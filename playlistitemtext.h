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

#ifndef PLAYLISTITEMTEXT_H
#define PLAYLISTITEMTEXT_H

#include <QTreeWidgetItem>
#include <QObject>
#include "playlistitem.h"
#include "textobject.h"
#include <QFont>

class PlaylistItemText : public PlaylistItem
{
public:
    PlaylistItemText(const QString &text, QTreeWidget* parent = NULL);

    ~PlaylistItemText();

    PlaylistItemType itemType();

    TextObject *displayObject() {return dynamic_cast<TextObject*>(p_displayObject);}    

private:

};

#endif // PLAYLISTITEMTEXT_H
