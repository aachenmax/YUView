/*  YUView - YUV player with advanced analytics toolset
*   Copyright (C) 2015  Institut für Nachrichtentechnik
*                       RWTH Aachen University, GERMANY
*
*   YUView is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   YUView is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with YUView.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "yuvfile.h"
#include <QFileInfo>
#include <QDir>
#include <QtEndian>
#include "math.h"
#include <cfloat>
#include <assert.h>
#if _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif


#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

inline quint32 SwapInt32(quint32 arg) {
    quint32 result;
    result = ((arg & 0xFF) << 24) | ((arg & 0xFF00) << 8) | ((arg >> 8) & 0xFF00) | ((arg >> 24) & 0xFF);
    return result;
}

inline quint16 SwapInt16(quint16 arg) {
    quint16 result;
    result = (quint16)(((arg << 8) & 0xFF00) | ((arg >> 8) & 0xFF));
    return result;
}

inline quint32 SwapInt32BigToHost(quint32 arg) {
#if __BIG_ENDIAN__ || IS_BIG_ENDIAN
    return arg;
#else
    return SwapInt32(arg);
#endif
}

inline quint32 SwapInt32LittleToHost(quint32 arg) {
#if __LITTLE_ENDIAN__ || IS_LITTLE_ENDIAN
    return arg;
#else
    return SwapInt32(arg);
#endif
}

inline quint16 SwapInt16LittleToHost(quint16 arg) {
#if __LITTLE_ENDIAN__
    return arg;
#else
    return SwapInt16(arg);
#endif
}

qint16 qFromLittleEndian(qint16 src);

// OVERALL CANDIDATE MODES, sieved more in each step
typedef struct {
 int width;
 int height;
 YUVCPixelFormatType pixelFormat;

 // flags set while checking
 bool   interesting;
 float mseY;
} candMode_t;

candMode_t candidateModes[] = {
    {176,144,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {352,240,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {352,288,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {480,480,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {480,576,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {704,480,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {720,480,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {704,576,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {720,576,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1024,768,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1280,720,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1280,960,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1920,1072,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1920,1080,YUVC_420YpCbCr8PlanarPixelFormat, false, 0.0 },
    {176,144,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {352,240,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {352,288,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {480,480,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {480,576,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {704,480,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {720,480,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {720,486,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {704,576,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {720,576,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1024,768,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1280,720,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1280,960,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1920,1072,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {1920,1080,YUVC_422YpCbCr8PlanarPixelFormat, false, 0.0 },
    {-1,-1, YUVC_UnknownPixelFormat, false, 0.0 }
};

std::map<YUVCPixelFormatType,PixelFormat> YUVFile::g_pixelFormatList = std::map<YUVCPixelFormatType,PixelFormat>();

YUVFile::YUVFile(const QString &fname, QObject *parent) : QObject(parent)
{
    p_srcFile = NULL;

    // open file for reading
    p_srcFile = new QFile(fname);
    p_srcFile->open(QIODevice::ReadOnly);

    // get some more information from file
    QFileInfo fileInfo(p_srcFile->fileName());
    p_path = fileInfo.path();
    p_createdtime = fileInfo.created().toString("yyyy-MM-dd hh:mm:ss");
    p_modifiedtime = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss");

    p_interpolationMode = BiLinearInterpolation;

    // preset internal values
    p_srcPixelFormat = YUVC_UnknownPixelFormat;
    p_interpolationMode = NearestNeighborInterpolation;
}

YUVFile::~YUVFile()
{
    delete p_srcFile;
}

std::map<YUVCPixelFormatType,PixelFormat> YUVFile::pixelFormatList()
{
    // create map of pixel format types
    if( g_pixelFormatList.empty() )
    {
        g_pixelFormatList[YUVC_UnknownPixelFormat].setParams("Unknown Pixel Format", 0, 0, 0, 0, 0, false);
        g_pixelFormatList[YUVC_GBR12in16LEPlanarPixelFormat].setParams("GBR 12-bit planar", 12, 48, 1, 1, 1, true);
        g_pixelFormatList[YUVC_32RGBAPixelFormat].setParams("RGBA 8-bit", 8, 32, 1, 1, 1, false);
        g_pixelFormatList[YUVC_24RGBPixelFormat].setParams("RGB 8-bit", 8, 24, 1, 1, 1, false);
        g_pixelFormatList[YUVC_24BGRPixelFormat].setParams("BGR 8-bit", 8, 24, 1, 1, 1, false);
        g_pixelFormatList[YUVC_444YpCbCr16LEPlanarPixelFormat].setParams("4:4:4 Y'CbCr 16-bit LE planar", 16, 48, 1, 1, 1, true);
        g_pixelFormatList[YUVC_444YpCbCr16BEPlanarPixelFormat].setParams("4:4:4 Y'CbCr 16-bit BE planar", 16, 48, 1, 1, 1, true);
        g_pixelFormatList[YUVC_444YpCbCr12LEPlanarPixelFormat].setParams("4:4:4 Y'CbCr 12-bit LE planar", 12, 48, 1, 1, 1, true);
        g_pixelFormatList[YUVC_444YpCbCr12BEPlanarPixelFormat].setParams("4:4:4 Y'CbCr 12-bit BE planar", 12, 48, 1, 1, 1, true);
        g_pixelFormatList[YUVC_444YpCbCr8PlanarPixelFormat].setParams("4:4:4 Y'CbCr 8-bit planar", 8, 24, 1, 1, 1, true);
        g_pixelFormatList[YUVC_444YpCrCb8PlanarPixelFormat].setParams("4:4:4 Y'CrCb 8-bit planar", 8, 24, 1, 1, 1, true);
        g_pixelFormatList[YUVC_422YpCbCr8PlanarPixelFormat].setParams("4:2:2 Y'CbCr 8-bit planar", 8, 16, 1, 2, 1, true);
        g_pixelFormatList[YUVC_422YpCrCb8PlanarPixelFormat].setParams("4:2:2 Y'CrCb 8-bit planar", 8, 16, 1, 2, 1, true);
        g_pixelFormatList[YUVC_UYVY422PixelFormat].setParams("4:2:2 8-bit packed", 8, 16, 1, 2, 1, false);
        g_pixelFormatList[YUVC_422YpCbCr10PixelFormat].setParams("4:2:2 10-bit packed 'v210'", 10, 128, 6, 2, 1, false);
        g_pixelFormatList[YUVC_UYVY422YpCbCr10PixelFormat].setParams("4:2:2 10-bit packed (UYVY)", 10, 128, 6, 2, 1, true);
        g_pixelFormatList[YUVC_420YpCbCr10LEPlanarPixelFormat].setParams("4:2:0 Y'CbCr 10-bit LE planar", 10, 24, 1, 2, 2, true,2);
        g_pixelFormatList[YUVC_420YpCbCr8PlanarPixelFormat].setParams("4:2:0 Y'CbCr 8-bit planar", 8, 12, 1, 2, 2, true);
        g_pixelFormatList[YUVC_411YpCbCr8PlanarPixelFormat].setParams("4:1:1 Y'CbCr 8-bit planar", 8, 12, 1, 4, 1, true);
        g_pixelFormatList[YUVC_8GrayPixelFormat].setParams("4:0:0 8-bit", 8, 8, 1, 0, 0, true);
    }

    return g_pixelFormatList;
}

void YUVFile::extractFormat(int* width, int* height, int* numFrames, double* frameRate)
{
    // preset return values
    int width1 = -1;
    int height1 = -1;
    int numFrames1 = -1;
    int width2 = -1;
    int height2 = -1;
    int numFrames2 = -1;
    int bitDepth = -1;

    double frameRate1 = -1, frameRate2 = -1;
    YUVCPixelFormatType cFormat1 = YUVC_UnknownPixelFormat, cFormat2 = YUVC_UnknownPixelFormat;

    // try to get information
    formatFromFilename(p_srcFile->fileName(), &width1, &height1, &frameRate1, &numFrames1,&bitDepth);
    formatFromCorrelation(&width2, &height2, &cFormat2, &numFrames2);

    // set return values
    *width = MAX( width1, width2 );
    *height = MAX( height1, height2 );
    p_srcPixelFormat = MAX( cFormat1, cFormat2 );
    *numFrames = MAX( numFrames1, numFrames2 );
    if (frameRate1 > 0 || frameRate2 > 0)
        *frameRate = MAX( frameRate1, frameRate2 );

    if(p_srcPixelFormat == YUVC_UnknownPixelFormat)
    {
        if (bitDepth==8)
        {
        p_srcPixelFormat = YUVC_420YpCbCr8PlanarPixelFormat;
        }
        else if (bitDepth==10)
        {
        p_srcPixelFormat = YUVC_420YpCbCr10LEPlanarPixelFormat;
        }
        else
        {
            //TODO
        }
    }
}

int YUVFile::getNumberFrames(int width, int height)
{
    qint64 fileSize = getFileSize();

    if(width > 0 && height > 0)
        return fileSize / bytesPerFrame(width, height, p_srcPixelFormat);
    else
        return -1;
}

int YUVFile::readFrame( QByteArray *targetBuffer, unsigned int frameIdx, int width, int height )
{
    if(p_srcFile == NULL)
        return 0;

    int bpf = bytesPerFrame(width, height, p_srcPixelFormat);
    int startPos = frameIdx * bpf;

    // check if our buffer is big enough
    if( targetBuffer->size() != bpf )
        targetBuffer->resize(bpf);

    // read bytes from file
    readBytes( targetBuffer->data(), startPos, bpf);

    return bpf;
}

void YUVFile::readBytes( char *targetBuffer, unsigned int startPos, unsigned int length )
{
    if(p_srcFile == NULL)
        return;

    p_srcFile->seek(startPos);
    p_srcFile->read(targetBuffer, length);
    //targetBuffer->setRawData( (const char*)p_srcFile->map(startPos, length, QFile::NoOptions), length );
}

float computeMSE( unsigned char *ptr, unsigned char *ptr2, int numPixels )
{
    float mse=0.0;
    float diff;

    if( numPixels > 0 )
    {
        for(int i=0; i<numPixels; i++)
        {
            diff = (float)ptr[i] - (float)ptr2[i];
            mse += diff*diff;
        }

        /* normalize on correlated pixels */
        mse /= (float)(numPixels);
    }

    return mse;
}

void YUVFile::formatFromFilename(QString filePath, int* width, int* height, double* frameRate, int* numFrames, int* bitDepth, bool isYUV)
{
    if(filePath.isEmpty())
        return;

    // preset return values first
    *width = -1;
    *height = -1;
    *frameRate = -1;
    *numFrames = -1;
    *bitDepth = -1;

    // parse filename and extract width, height and framerate
    // default format is: sequenceName_widthxheight_framerate.yuv
    QRegExp rxExtended("([0-9]+)x([0-9]+)_([0-9]+)_([0-9]+)");
    QRegExp rxDefault("([0-9]+)x([0-9]+)_([0-9]+)");
    if(rxExtended.indexIn(filePath) > -1)
    {
        QString widthString = rxExtended.cap(1);
        *width = widthString.toInt();

        QString heightString = rxExtended.cap(2);
        *height = heightString.toInt();

        QString rateString = rxExtended.cap(3);
        *frameRate = rateString.toDouble();

        QString bitDepthString = rxExtended.cap(4);
        *bitDepth = bitDepthString.toInt();
    }
    else if (rxDefault.indexIn(filePath) > -1 ) {
        QString widthString = rxDefault.cap(1);
        *width = widthString.toInt();

        QString heightString = rxDefault.cap(2);
        *height = heightString.toInt();

        QString rateString = rxDefault.cap(3);
        *frameRate = rateString.toDouble();

        *bitDepth = 8; // assume 8 bit
    }
    else
    {
        // try to find resolution indicators (e.g. 'cif', 'hd') in file name
        if( filePath.contains("_cif", Qt::CaseInsensitive) )
        {
            *width = 352;
            *height = 288;
        }
        else if( filePath.contains("_qcif", Qt::CaseInsensitive) )
        {
            *width = 176;
            *height = 144;
        }
        else if( filePath.contains("_4cif", Qt::CaseInsensitive) )
        {
            *width = 704;
            *height = 576;
        }
    }

    if(isYUV && *width > 0 && *height > 0 && *bitDepth > 0)
    {
        QFileInfo fileInfo(filePath);
        qint64 fileSize = fileInfo.size();
        if (*bitDepth==8)
        {
        *numFrames = fileSize / YUVFile::bytesPerFrame(*width, *height, YUVC_420YpCbCr8PlanarPixelFormat); // assume 4:2:0, 8bit
        }
        else if (*bitDepth==10)
        {
        *numFrames = fileSize / YUVFile::bytesPerFrame(*width, *height, YUVC_420YpCbCr10LEPlanarPixelFormat); // assume 4:2:0, 10bit
        }
        else
        {
            // do other stuff
        }
    }
}

void YUVFile::formatFromCorrelation(int* width, int* height, YUVCPixelFormatType* cFormat, int* numFrames)
{
    if(p_srcFile->fileName().isEmpty())
        return;

    unsigned char *ptr;
    float leastMSE, mse;
    int   bestMode;

    // step1: file size must be a multiple of w*h*(color format)
    QFileInfo fileInfo(*p_srcFile);
    qint64 fileSize = fileInfo.size();
    qint64 picSize;

    if(fileSize < 1)
        return;

    // if any candidate exceeds file size for two frames, discard
    // if any candidate does not represent a multiple of file size, discard
    int i = 0;
    bool found = false;
    while( candidateModes[i].pixelFormat != YUVC_UnknownPixelFormat )
    {
        /* one pic in bytes */
        picSize = bytesPerFrame(candidateModes[i].width, candidateModes[i].height, candidateModes[i].pixelFormat);

        if( fileSize >= (picSize*2) )    // at least 2 pics for correlation analysis
        {
            if( (fileSize % picSize) == 0 ) // important: file size must be multiple of pic size
            {
                candidateModes[i].interesting = true; // test passed
                found = true;
            }
        }

        i++;
    };

    if( !found  ) // no proper candidate mode ?
        return;

    // step2: calculate max. correlation for first two frames, use max. candidate frame size
    i=0;
    while( candidateModes[i].pixelFormat != YUVC_UnknownPixelFormat )
    {
        if( candidateModes[i].interesting )
        {
            picSize = bytesPerFrame(candidateModes[i].width, candidateModes[i].height, candidateModes[i].pixelFormat);

            QByteArray yuvBytes(picSize*2, 0);

            readBytes(yuvBytes.data(), 0, picSize*2);

            // assumptions: YUV is planar (can be changed if necessary)
            // only check mse in luminance
            ptr  = (unsigned char*) yuvBytes.data();
            candidateModes[i].mseY = computeMSE( ptr, ptr + picSize, picSize );
        }
        i++;
    };

    // step3: select best candidate
    i=0;
    leastMSE = FLT_MAX; // large error...
    bestMode = 0;
    while( candidateModes[i].pixelFormat != YUVC_UnknownPixelFormat )
    {
        if( candidateModes[i].interesting )
        {
            mse = (candidateModes[i].mseY);
            if( mse < leastMSE )
            {
                bestMode = i;
                leastMSE = mse;
            }
        }
        i++;
    };

    if( leastMSE < 100 )
    {
        *width  = candidateModes[bestMode].width;
        *height = candidateModes[bestMode].height;
        *cFormat = candidateModes[bestMode].pixelFormat;
        *numFrames = fileSize / bytesPerFrame(*width, *height, *cFormat);
    }

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QString YUVFile::fileName()
{
    p_path = p_srcFile->fileName();
    QStringList components = p_srcFile->fileName().split(QDir::separator());
    return components.last();
}

qint64 YUVFile::getFileSize()
{
    QFileInfo fileInfo(p_srcFile->fileName());
    return fileInfo.size();
}

// Check if the size/format/nrBytes makes sense
QString YUVFile::getStatus(int width, int height)
{
  qint64 nrBytes = getFileSize();
  int nrBytesPerFrame = bytesPerFrame(width, height, p_srcPixelFormat);
  if (nrBytes % nrBytesPerFrame != 0)
  {
    // Division is with residual
    return QString("Error: File Size and resolution do not match.");
  }
  return QString("OK");
}

void YUVFile::getOneFrame(QByteArray* targetByteArray, unsigned int frameIdx, int width, int height )
{
    // check if we need to do chroma upsampling
    if(p_srcPixelFormat != YUVC_444YpCbCr8PlanarPixelFormat && p_srcPixelFormat != YUVC_444YpCbCr12NativePlanarPixelFormat && p_srcPixelFormat != YUVC_444YpCbCr16NativePlanarPixelFormat && p_srcPixelFormat != YUVC_24RGBPixelFormat )
    {
        // read one frame into temporary buffer
        readFrame( &p_tmpBufferYUV, frameIdx, width, height);
        //use dummy data to check
        // convert original data format into YUV444 planar format
        convert2YUV444(&p_tmpBufferYUV, width, height, targetByteArray);
    }
    else    // source and target format are identical --> no conversion necessary
    {
        // read one frame into cached frame (already in YUV444 format)
        readFrame( targetByteArray, frameIdx, width, height);
    }
}

void YUVFile::convert2YUV444(QByteArray *sourceBuffer, int lumaWidth, int lumaHeight, QByteArray *targetBuffer)
{
    const int componentWidth = lumaWidth;
    const int componentHeight = lumaHeight;
    // TODO: make this compatible with 10bit sequences
    const int componentLength = componentWidth*componentHeight; // number of bytes per luma frames
    const int horiSubsampling = horizontalSubSampling(p_srcPixelFormat);
    const int vertSubsampling = verticalSubSampling(p_srcPixelFormat);
    const int chromaWidth = horiSubsampling==0 ? 0 : lumaWidth / horiSubsampling;
    const int chromaHeight = vertSubsampling == 0 ? 0 : lumaHeight / vertSubsampling;
    const int chromaLength = chromaWidth * chromaHeight; // number of bytes per chroma frame

    // make sure target buffer is big enough (YUV444 means 3 byte per sample)
    int targetBufferLength = 3*componentWidth*componentHeight*bytePerComponent(p_srcPixelFormat);
    if( targetBuffer->size() != targetBufferLength )
        targetBuffer->resize(targetBufferLength);

    // TODO: keep unsigned char for 10bit? use short?
    if (chromaLength == 0) {
        const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
        unsigned char *dstY = (unsigned char*)targetBuffer->data();
        unsigned char *dstU = dstY + componentLength;
        memcpy(dstY, srcY, componentLength);
        memset(dstU, 128, 2*componentLength);
    } else if (p_srcPixelFormat == YUVC_UYVY422PixelFormat) {
        const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
        unsigned char *dstY = (unsigned char*)targetBuffer->data();
        unsigned char *dstU = dstY + componentLength;
        unsigned char *dstV = dstU + componentLength;

        int y;
#pragma omp parallel for default(none) shared(dstY,dstU,dstV,srcY)
        for (y = 0; y < componentHeight; y++) {
            for (int x = 0; x < componentWidth; x++) {
                dstY[x + y*componentWidth] = srcY[((x+y*componentWidth)<<1)+1];
                dstU[x + y*componentWidth] = srcY[((((x>>1)<<1)+y*componentWidth)<<1)];
                dstV[x + y*componentWidth] = srcY[((((x>>1)<<1)+y*componentWidth)<<1)+2];
            }
        }
    } else if (p_srcPixelFormat == YUVC_UYVY422YpCbCr10PixelFormat) {
        const quint32 *srcY = (quint32*)sourceBuffer->data();
        quint16 *dstY = (quint16*)targetBuffer->data();
        quint16 *dstU = dstY + componentLength;
        quint16 *dstV = dstU + componentLength;

        int i;
#define BIT_INCREASE 6
#pragma omp parallel for default(none) shared(dstY,dstU,dstV,srcY)
        for (i = 0; i < ((componentLength+5)/6); i++) {
            const int srcPos = i*4;
            const int dstPos = i*6;
            quint32 srcVal;
            srcVal = SwapInt32BigToHost(srcY[srcPos]);
            dstV[dstPos]   = dstV[dstPos+1] = (srcVal&0xffc00000)>>(22-BIT_INCREASE);
            dstY[dstPos]   =                  (srcVal&0x003ff000)>>(12-BIT_INCREASE);
            dstU[dstPos]   = dstU[dstPos+1] = (srcVal&0x00000ffc)<<(BIT_INCREASE-2);
            srcVal = SwapInt32BigToHost(srcY[srcPos+1]);
            dstY[dstPos+1] =                  (srcVal&0xffc00000)>>(22-BIT_INCREASE);
            dstV[dstPos+2] = dstV[dstPos+3] = (srcVal&0x003ff000)>>(12-BIT_INCREASE);
            dstY[dstPos+2] =                  (srcVal&0x00000ffc)<<(BIT_INCREASE-2);
            srcVal = SwapInt32BigToHost(srcY[srcPos+2]);
            dstU[dstPos+2] = dstU[dstPos+3] = (srcVal&0xffc00000)>>(22-BIT_INCREASE);
            dstY[dstPos+3] =                  (srcVal&0x003ff000)>>(12-BIT_INCREASE);
            dstV[dstPos+4] = dstV[dstPos+5] = (srcVal&0x00000ffc)<<(BIT_INCREASE-2);
            srcVal = SwapInt32BigToHost(srcY[srcPos+3]);
            dstY[dstPos+4] =                  (srcVal&0xffc00000)>>(22-BIT_INCREASE);
            dstU[dstPos+4] = dstU[dstPos+5] = (srcVal&0x003ff000)>>(12-BIT_INCREASE);
            dstY[dstPos+5] =                  (srcVal&0x00000ffc)<<(BIT_INCREASE-2);
        }
    } else if (p_srcPixelFormat == YUVC_422YpCbCr10PixelFormat) {
        const quint32 *srcY = (quint32*)sourceBuffer->data();
        quint16 *dstY = (quint16*)targetBuffer->data();
        quint16 *dstU = dstY + componentLength;
        quint16 *dstV = dstU + componentLength;

        int i;
#define BIT_INCREASE 6
#pragma omp parallel for default(none) shared(dstY,dstU,dstV,srcY)
        for (i = 0; i < ((componentLength+5)/6); i++) {
            const int srcPos = i*4;
            const int dstPos = i*6;
            quint32 srcVal;
            srcVal = SwapInt32LittleToHost(srcY[srcPos]);
            dstV[dstPos]   = dstV[dstPos+1] = (srcVal&0x3ff00000)>>(20-BIT_INCREASE);
            dstY[dstPos]   =                  (srcVal&0x000ffc00)>>(10-BIT_INCREASE);
            dstU[dstPos]   = dstU[dstPos+1] = (srcVal&0x000003ff)<<BIT_INCREASE;
            srcVal = SwapInt32LittleToHost(srcY[srcPos+1]);
            dstY[dstPos+1] =                  (srcVal&0x000003ff)<<BIT_INCREASE;
            dstU[dstPos+2] = dstU[dstPos+3] = (srcVal&0x000ffc00)>>(10-BIT_INCREASE);
            dstY[dstPos+2] =                  (srcVal&0x3ff00000)>>(20-BIT_INCREASE);
            srcVal = SwapInt32LittleToHost(srcY[srcPos+2]);
            dstU[dstPos+4] = dstU[dstPos+5] = (srcVal&0x3ff00000)>>(20-BIT_INCREASE);
            dstY[dstPos+3] =                  (srcVal&0x000ffc00)>>(10-BIT_INCREASE);
            dstV[dstPos+2] = dstV[dstPos+3] = (srcVal&0x000003ff)<<BIT_INCREASE;
            srcVal = SwapInt32LittleToHost(srcY[srcPos+3]);
            dstY[dstPos+4] =                  (srcVal&0x000003ff)<<BIT_INCREASE;
            dstV[dstPos+4] = dstV[dstPos+5] = (srcVal&0x000ffc00)>>(10-BIT_INCREASE);
            dstY[dstPos+5] =                  (srcVal&0x3ff00000)>>(20-BIT_INCREASE);
        }
    } else if (p_srcPixelFormat == YUVC_420YpCbCr8PlanarPixelFormat && p_interpolationMode == BiLinearInterpolation) {
        // vertically midway positioning - unsigned rounding
        const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
        const unsigned char *srcU = srcY + componentLength;
        const unsigned char *srcV = srcU + chromaLength;
        const unsigned char *srcUV[2] = {srcU, srcV};
        unsigned char *dstY = (unsigned char*)targetBuffer->data();
        unsigned char *dstU = dstY + componentLength;
        unsigned char *dstV = dstU + componentLength;
        unsigned char *dstUV[2] = {dstU, dstV};

        const int dstLastLine = (componentHeight-1)*componentWidth;
        const int srcLastLine = (chromaHeight-1)*chromaWidth;

        memcpy(dstY, srcY, componentLength);

        int c;
        for (c = 0; c < 2; c++) {
            //NSLog(@"%i", omp_get_num_threads());
            // first line
            dstUV[c][0] = srcUV[c][0];
            int i;
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (i = 0; i < chromaWidth-1; i++) {
                dstUV[c][i*2+1] = (( (int)(srcUV[c][i]) + (int)(srcUV[c][i+1]) + 1 ) >> 1);
                dstUV[c][i*2+2] = srcUV[c][i+1];
            }
            dstUV[c][componentWidth-1] = dstUV[c][componentWidth-2];

            int j;
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (j = 0; j < chromaHeight-1; j++) {
                const int dstTop = (j*2+1)*componentWidth;
                const int dstBot = (j*2+2)*componentWidth;
                const int srcTop = j*chromaWidth;
                const int srcBot = (j+1)*chromaWidth;
                dstUV[c][dstTop] = (( 3*(int)(srcUV[c][srcTop]) +   (int)(srcUV[c][srcBot]) + 2 ) >> 2);
                dstUV[c][dstBot] = ((   (int)(srcUV[c][srcTop]) + 3*(int)(srcUV[c][srcBot]) + 2 ) >> 2);
                for (int i = 0; i < chromaWidth-1; i++) {
                    const int tl = srcUV[c][srcTop+i];
                    const int tr = srcUV[c][srcTop+i+1];
                    const int bl = srcUV[c][srcBot+i];
                    const int br = srcUV[c][srcBot+i+1];
                    dstUV[c][dstTop+i*2+1] = (( 6*tl + 6*tr + 2*bl + 2*br + 8 ) >> 4);
                    dstUV[c][dstBot+i*2+1] = (( 2*tl + 2*tr + 6*bl + 6*br + 8 ) >> 4);
                    dstUV[c][dstTop+i*2+2] = ((        3*tr +          br + 2 ) >> 2);
                    dstUV[c][dstBot+i*2+2] = ((          tr +        3*br + 2 ) >> 2);
                }
                dstUV[c][dstTop+componentWidth-1] = dstUV[c][dstTop+componentWidth-2];
                dstUV[c][dstBot+componentWidth-1] = dstUV[c][dstBot+componentWidth-2];
            }

            dstUV[c][dstLastLine] = srcUV[c][srcLastLine];
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (i = 0; i < chromaWidth-1; i++) {
                dstUV[c][dstLastLine+i*2+1] = (( (int)(srcUV[c][srcLastLine+i]) + (int)(srcUV[c][srcLastLine+i+1]) + 1 ) >> 1);
                dstUV[c][dstLastLine+i*2+2] = srcUV[c][srcLastLine+i+1];
            }
            dstUV[c][dstLastLine+componentWidth-1] = dstUV[c][dstLastLine+componentWidth-2];
        }
    } else if (p_srcPixelFormat == YUVC_420YpCbCr8PlanarPixelFormat && p_interpolationMode == InterstitialInterpolation) {
        // interstitial positioning - unsigned rounding, takes 2 times as long as nearest neighbour
        const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
        const unsigned char *srcU = srcY + componentLength;
        const unsigned char *srcV = srcU + chromaLength;
        const unsigned char *srcUV[2] = {srcU, srcV};
        unsigned char *dstY = (unsigned char*)targetBuffer->data();
        unsigned char *dstU = dstY + componentLength;
        unsigned char *dstV = dstU + componentLength;
        unsigned char *dstUV[2] = {dstU, dstV};

        const int dstLastLine = (componentHeight-1)*componentWidth;
        const int srcLastLine = (chromaHeight-1)*chromaWidth;

        memcpy(dstY, srcY, componentLength);

        int c;
        for (c = 0; c < 2; c++) {
            // first line
            dstUV[c][0] = srcUV[c][0];

            int i;
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (i = 0; i < chromaWidth-1; i++) {
                dstUV[c][2*i+1] = ((3*(int)(srcUV[c][i]) +   (int)(srcUV[c][i+1]) + 2)>>2);
                dstUV[c][2*i+2] = ((  (int)(srcUV[c][i]) + 3*(int)(srcUV[c][i+1]) + 2)>>2);
            }
            dstUV[c][componentWidth-1] = srcUV[c][chromaWidth-1];

            int j;
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (j = 0; j < chromaHeight-1; j++) {
                const int dstTop = (j*2+1)*componentWidth;
                const int dstBot = (j*2+2)*componentWidth;
                const int srcTop = j*chromaWidth;
                const int srcBot = (j+1)*chromaWidth;
                dstUV[c][dstTop] = (( 3*(int)(srcUV[c][srcTop]) +   (int)(srcUV[c][srcBot]) + 2 ) >> 2);
                dstUV[c][dstBot] = ((   (int)(srcUV[c][srcTop]) + 3*(int)(srcUV[c][srcBot]) + 2 ) >> 2);
                for (int i = 0; i < chromaWidth-1; i++) {
                    const int tl = srcUV[c][srcTop+i];
                    const int tr = srcUV[c][srcTop+i+1];
                    const int bl = srcUV[c][srcBot+i];
                    const int br = srcUV[c][srcBot+i+1];
                    dstUV[c][dstTop+i*2+1] = (9*tl + 3*tr + 3*bl +   br + 8) >> 4;
                    dstUV[c][dstBot+i*2+1] = (3*tl +   tr + 9*bl + 3*br + 8) >> 4;
                    dstUV[c][dstTop+i*2+2] = (3*tl + 9*tr +   bl + 3*br + 8) >> 4;
                    dstUV[c][dstBot+i*2+2] = (  tl + 3*tr + 3*bl + 9*br + 8) >> 4;
                }
                dstUV[c][dstTop+componentWidth-1] = (( 3*(int)(srcUV[c][srcTop+chromaWidth-1]) +   (int)(srcUV[c][srcBot+chromaWidth-1]) + 2 ) >> 2);
                dstUV[c][dstBot+componentWidth-1] = ((   (int)(srcUV[c][srcTop+chromaWidth-1]) + 3*(int)(srcUV[c][srcBot+chromaWidth-1]) + 2 ) >> 2);
            }

            dstUV[c][dstLastLine] = srcUV[c][srcLastLine];
#pragma omp parallel for default(none) shared(dstUV,srcUV) firstprivate(c)
            for (i = 0; i < chromaWidth-1; i++) {
                dstUV[c][dstLastLine+i*2+1] = ((3*(int)(srcUV[c][srcLastLine+i]) +   (int)(srcUV[c][srcLastLine+i+1]) + 2)>>2);
                dstUV[c][dstLastLine+i*2+2] = ((  (int)(srcUV[c][srcLastLine+i]) + 3*(int)(srcUV[c][srcLastLine+i+1]) + 2)>>2);
            }
            dstUV[c][dstLastLine+componentWidth-1] = srcUV[c][srcLastLine+chromaWidth-1];
        }
    } /*else if (pixelFormatType == YUVC_420YpCbCr8PlanarPixelFormat && self.chromaInterpolation == 3) {
           // interstitial positioning - correct signed rounding - takes 6/5 times as long as unsigned rounding
           const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
           const unsigned char *srcU = srcY + componentLength;
           const unsigned char *srcV = srcU + chromaLength;
           unsigned char *dstY = (unsigned char*)targetBuffer->data();
           unsigned char *dstU = dstY + componentLength;
           unsigned char *dstV = dstU + componentLength;

           memcpy(dstY, srcY, componentLength);

           unsigned char *dstC = dstU;
           const unsigned char *srcC = srcU;
           int c;
           for (c = 0; c < 2; c++) {
              // first line
              unsigned char *endLine = dstC + componentWidth;
              unsigned char *endComp = dstC + componentLength;
              *dstC++ = *srcC;
              while (dstC < (endLine-1)) {
                 *dstC++ = thresAddAndShift( 3*(int)(*srcC) +   (int)(*(srcC+1)), 512, 1, 2, 2);
                 *dstC++ = thresAddAndShift(   (int)(*srcC) + 3*(int)(*(srcC+1)), 512, 1, 2, 2);
                 srcC++;
              }
              *dstC++ = *srcC++;
              srcC -= chromaWidth;

              while (dstC < endComp - 2*componentWidth) {
                 endLine = dstC + componentWidth;
                 *(dstC)                = thresAddAndShift( 3*(int)(*srcC) +   (int)(*(srcC+chromaWidth)), 512, 1, 2, 2);
                 *(dstC+componentWidth) = thresAddAndShift(   (int)(*srcC) + 3*(int)(*(srcC+chromaWidth)), 512, 1, 2, 2);
                 dstC++;
                 while (dstC < endLine-1) {
                    int tl = (int)*srcC;
                    int tr = (int)*(srcC+1);
                    int bl = (int)*(srcC+chromaWidth);
                    int br = (int)*(srcC+chromaWidth+1);
                    *(dstC)                  = thresAddAndShift(9*tl + 3*tr + 3*bl +   br, 2048, 7, 8, 4);
                    *(dstC+1)                = thresAddAndShift(3*tl + 9*tr +   bl + 3*br, 2048, 7, 8, 4);
                    *(dstC+componentWidth)   = thresAddAndShift(3*tl +   tr + 9*bl + 3*br, 2048, 7, 8, 4);
                    *(dstC+componentWidth+1) = thresAddAndShift(  tl + 3*tr + 3*bl + 9*br, 2048, 7, 8, 4);
                    srcC++;
                    dstC+=2;
                 }
                 *(dstC)                = thresAddAndShift( 3*(int)(*srcC) +   (int)(*(srcC+chromaWidth)), 512, 1, 2, 2);
                 *(dstC+componentWidth) = thresAddAndShift(   (int)(*srcC) + 3*(int)(*(srcC+chromaWidth)), 512, 1, 2, 2);
                 dstC++;
                 srcC++;
                 dstC += componentWidth;
              }

              endLine = dstC + componentWidth;
              *dstC++ = *srcC;
              while (dstC < (endLine-1)) {
                 *dstC++ = thresAddAndShift( 3*(int)(*srcC) +   (int)(*(srcC+1)), 512, 1, 2, 2);
                 *dstC++ = thresAddAndShift(   (int)(*srcC) + 3*(int)(*(srcC+1)), 512, 1, 2, 2);
                 srcC++;
              }
              *dstC++ = *srcC++;

              dstC = dstV;
              srcC = srcV;
           }
         }*/ else if (isPlanar(p_srcPixelFormat) && bitsPerSample(p_srcPixelFormat) == 8) {
        // sample and hold interpolation
        const bool reverseUV = (p_srcPixelFormat == YUVC_444YpCrCb8PlanarPixelFormat) || (p_srcPixelFormat == YUVC_422YpCrCb8PlanarPixelFormat);
        const unsigned char *srcY = (unsigned char*)sourceBuffer->data();
        const unsigned char *srcU = srcY + componentLength + (reverseUV?chromaLength:0);
        const unsigned char *srcV = srcY + componentLength + (reverseUV?0:chromaLength);
        unsigned char *dstY = (unsigned char*)targetBuffer->data();
        unsigned char *dstU = dstY + componentLength;
        unsigned char *dstV = dstU + componentLength;
        int horiShiftTmp = 0;
        int vertShiftTmp = 0;
        while (((1<<horiShiftTmp) & horiSubsampling) != 0) horiShiftTmp++;
        while (((1<<vertShiftTmp) & vertSubsampling) != 0) vertShiftTmp++;
        const int horiShift = horiShiftTmp;
        const int vertShift = vertShiftTmp;

        memcpy(dstY, srcY, componentLength);

        if (2 == horiSubsampling && 2 == vertSubsampling) {
            int y;
#pragma omp parallel for default(none) shared(dstV,dstU,srcV,srcU)
            for (y = 0; y < chromaHeight; y++) {
                for (int x = 0; x < chromaWidth; x++) {
                    dstU[2*x + 2*y*componentWidth] = dstU[2*x+1 + 2*y*componentWidth] = srcU[x + y*chromaWidth];
                    dstV[2*x + 2*y*componentWidth] = dstV[2*x+1 + 2*y*componentWidth] = srcV[x + y*chromaWidth];
                }
                memcpy(&dstU[(2*y+1)*componentWidth], &dstU[(2*y)*componentWidth], componentWidth);
                memcpy(&dstV[(2*y+1)*componentWidth], &dstV[(2*y)*componentWidth], componentWidth);
            }
        } else if ((1<<horiShift) == horiSubsampling && (1<<vertShift) == vertSubsampling) {
            int y;
#pragma omp parallel for default(none) shared(dstV,dstU,srcV,srcU)
            for (y = 0; y < componentHeight; y++) {
                for (int x = 0; x < componentWidth; x++) {
                    //dstY[x + y*componentWidth] = srcY[x + y*componentWidth];
                    dstU[x + y*componentWidth] = srcU[(x>>horiShift) + (y>>vertShift)*chromaWidth];
                    dstV[x + y*componentWidth] = srcV[(x>>horiShift) + (y>>vertShift)*chromaWidth];
                }
            }
        } else {
            int y;
#pragma omp parallel for default(none) shared(dstV,dstU,srcV,srcU)
            for (y = 0; y < componentHeight; y++) {
                for (int x = 0; x < componentWidth; x++) {
                    //dstY[x + y*componentWidth] = srcY[x + y*componentWidth];
                    dstU[x + y*componentWidth] = srcU[x/horiSubsampling + y/vertSubsampling*chromaWidth];
                    dstV[x + y*componentWidth] = srcV[x/horiSubsampling + y/vertSubsampling*chromaWidth];
                }
            }
        }
    } else if (p_srcPixelFormat == YUVC_420YpCbCr10LEPlanarPixelFormat) {
        // TODO: chroma interpolation for 4:2:0 10bit planar
        const unsigned short *srcY = (unsigned short*)sourceBuffer->data();
        const unsigned short *srcU = srcY + componentLength;
        const unsigned short *srcV = srcU + chromaLength;
        unsigned short *dstY = (unsigned short*)targetBuffer->data();
        unsigned short *dstU = dstY + componentLength;
        unsigned short *dstV = dstU + componentLength;

        int y;
#pragma omp parallel for default(none) shared(dstY,dstV,dstU,srcY,srcV,srcU)
        for (y = 0; y < componentHeight; y++) {
            for (int x = 0; x < componentWidth; x++) {
                //dstY[x + y*componentWidth] = MIN(1023, CFSwapInt16LittleToHost(srcY[x + y*componentWidth])) << 6; // clip value for data which exceeds the 2^10-1 range
           //     dstY[x + y*componentWidth] = SwapInt16LittleToHost(srcY[x + y*componentWidth])<<6;
            //    dstU[x + y*componentWidth] = SwapInt16LittleToHost(srcU[x/2 + (y/2)*chromaWidth])<<6;
            //    dstV[x + y*componentWidth] = SwapInt16LittleToHost(srcV[x/2 + (y/2)*chromaWidth])<<6;
                dstY[x + y*componentWidth] = qFromLittleEndian(srcY[x + y*componentWidth]);
                dstY[x + y*componentWidth] = qFromLittleEndian(srcY[x + y*componentWidth]);
                dstU[x + y*componentWidth] = qFromLittleEndian(srcU[x/2 + (y/2)*chromaWidth]);
                dstV[x + y*componentWidth] = qFromLittleEndian(srcV[x/2 + (y/2)*chromaWidth]);
            }
        }
    }
    else if (   p_srcPixelFormat == YUVC_444YpCbCr12SwappedPlanarPixelFormat
                  || p_srcPixelFormat == YUVC_444YpCbCr16SwappedPlanarPixelFormat)
    {
        swab((char*)sourceBuffer->data(), (char*)targetBuffer->data(), bytesPerFrame(componentWidth,componentHeight,p_srcPixelFormat));
    } else {
        printf("Unhandled pixel format: %d\n", p_srcPixelFormat);
    }

    return;
}

// static members to get information about pixel formats
int YUVFile::verticalSubSampling(YUVCPixelFormatType pixelFormat)  { return pixelFormatList().count(pixelFormat)?pixelFormatList()[pixelFormat].subsamplingVertical():0; }
int YUVFile::horizontalSubSampling(YUVCPixelFormatType pixelFormat) { return pixelFormatList().count(pixelFormat)?pixelFormatList()[pixelFormat].subsamplingHorizontal():0; }
int YUVFile::bitsPerSample(YUVCPixelFormatType pixelFormat)  { return pixelFormatList().count(pixelFormat)?pixelFormatList()[pixelFormat].bitsPerSample():0; }
int YUVFile::bytePerComponent(YUVCPixelFormatType pixelFormat) {return pixelFormatList().count(pixelFormat)?pixelFormatList()[pixelFormat].bytePerComponent():0;}
int YUVFile::bytesPerFrame(int width, int height, YUVCPixelFormatType cFormat)
{
    if(pixelFormatList().count(cFormat) == 0)
        return 0;

    unsigned numSamples = width*height;
    unsigned remainder = numSamples % pixelFormatList()[cFormat].bitsPerPixelDenominator();
    unsigned bits = numSamples / pixelFormatList()[cFormat].bitsPerPixelDenominator();
    if (remainder == 0) {
        bits *= pixelFormatList()[cFormat].bitsPerPixelNominator();
    } else {
        printf("warning: pixels not divisable by bpp denominator for pixel format '%d' - rounding up\n", cFormat);
        bits = (bits+1) * pixelFormatList()[cFormat].bitsPerPixelNominator();
    }
    if (bits % 8 != 0) {
        printf("warning: bits not divisible by 8 for pixel format '%d' - rounding up\n", cFormat);
        bits += 8;
    }

    return bits/8;
}
bool YUVFile::isPlanar(YUVCPixelFormatType pixelFormat) { return pixelFormatList().count(pixelFormat)?pixelFormatList()[pixelFormat].isPlanar():false; }

