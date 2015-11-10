/*
    Copyright 2015 StapleButter

    This file is part of CoinKiller.

    CoinKiller is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    CoinKiller is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along
    with CoinKiller. If not, see http://www.gnu.org/licenses/.
*/

#include "level.h"
#include "game.h"
#include "objects.h"
#include "unitsconvert.h"

Level::Level(Game *game, int world, int level, int area)
{
    this->game = game;

    this->world = world;
    this->level = level;

    QString arcpath = QString("/Course/%1-%2.sarc").arg(world).arg(level);
    qDebug(arcpath.toStdString().c_str());
    archive = new SarcFilesystem(game->fs->openFile(arcpath));
    this->area = area;


    QString headerfile = QString("/course/course%1.bin").arg(area);
    if (!archive->fileExists(headerfile))
        throw std::runtime_error("Level has no header!!");

    // read header blocks
    FileBase* header = archive->openFile(headerfile);
    header->open();
    header->seek(0);

    quint32 blockOffsets[17];
    quint32 blockSizes[17];
    for (int b = 0; b < 17; b++)
    {
        blockOffsets[b] = header->read32();
        blockSizes[b] = header->read32();
    }

    // Block 0: Tilesets
    for (int t = 0; t < 4; t++)
    {
        header->seek(blockOffsets[0] + (t*32));

        QString tilesetname;
        header->readStringASCII(tilesetname, 32);

        if (tilesetname.isEmpty())
        {
            tilesets[t] = NULL;
            continue;
        }

        try
        {
            tilesets[t] = game->getTileset(tilesetname);
        }
        catch ( const std::exception & e )
        {
            qDebug("Tileset %s not found", tilesetname.toStdString().c_str());
            tilesets[t] = NULL;
        }
    }

    // Block 1: Area Settings
    header->seek(blockOffsets[1]);
    header->skip(10); // 8 Zeros + Unk1
    timeLimit = header->read16();
    qDebug("Time Limit: %d", timeLimit);

    // Block 2: Zone Boundings
    header->seek(blockOffsets[2]);
    upperbound = header->read32();
    lowerbound = header->read32();
    unkbound1 = header->read32();
    unkbound2 = header->read32();
    boundingid = header->read16();
    unk1 = header->read16();
    header->skip(8);
    qDebug("Bounding block found! Uppper Bound: %d, Lower Bound: %d, Unknown Upper Bound: %d, Unknown Lower Bound: %d, Bounding ID: %d, Unknown 0x12: %d", upperbound, lowerbound, unkbound1, unkbound2, boundingid, unk1);

    // Block 4: Background Information
    header->seek(blockOffsets[4]);
    QString bgname;
    bgblockid = header->read16();
    xscrollrate = header->read8();
    yscrollrate = header->read8();
    xpos = header->read8();
    ypos = header->read8();
    header->skip(2);
    header->readStringASCII(bgname, 16);
    unk1 = header->read16();
    header->skip(2);
    qDebug("Background Found! BG ID: %d, X Scroll Rate: %d, Y Scroll Rate: %d, X Position: %d, Y Position: %d, Name: %s, Unknown 0x18: %d", bgblockid, xscrollrate, yscrollrate, xpos, ypos, bgname.toStdString().c_str(), unk1);

    // Block 6: Entrances
    header->seek(blockOffsets[6]);
    for (int e = 0; e < (int)(blockSizes[6]/24); e++)
    {
        Entrance* entr = new Entrance(to20(header->read16()), to20(header->read16()), header->read16(), header->read16(), header->read8(), header->read8(), header->read8(), header->read8());
        qDebug("Found Entrance with x: %d, y: %d", entr->getx(), entr->gety());
        entrances.append(*entr);

        header->skip(12); // data we don't care about right now (id is in here, too. For now simply e)
    }

    // Block 7: Sprites
    header->seek(blockOffsets[7]);
    for (;;)
    {
        quint16 id = header->read16();
        if (id == 0xFFFF) break;

        Sprite* spr = new Sprite(to20(header->read16()), to20(header->read16()), id);

        for (int i=0; i<8; i++) spr->setByte(i, header->read8());

        spr->setRect();
        sprites.append(*spr);

        header->skip(10); // Unused Sprite Data and Zone
    }

    // Block 9: Zones
    header->seek(blockOffsets[9]);
    for (int z = 0; z < (int)(blockSizes[9]/24); z++)
    {
        Zone* zone = new Zone(to20(header->read16()), to20(header->read16()), to20(header->read16()), to20(header->read16()), z);
        qDebug("Found Zone with x: %d, y: %d, width: %d, height: %d", zone->getx(), zone->gety(), zone->getwidth(), zone->getheight());
        zones.append(*zone);

        header->skip(20); // data we don't care about right now (id is in here, too. For now simply z)
    }

    // Block 10: Locations
    header->seek(blockOffsets[10]);
    for (int l = 0; l < (int)(blockSizes[10]/12); l++)
    {
        Location* loc = new Location(to20(header->read16()), to20(header->read16()), to20(header->read16()), to20(header->read16()), header->read8());
        qDebug("Found Location with x: %d, y: %d, width: %d, height: %d", loc->getx(), loc->gety(), loc->getwidth(), loc->getheight());
        locations.append(*loc);

        header->skip(3);
    }

    // Block 13/14: Paths
    for (int p = 0; p < (int)(blockSizes[13]/12); p++)
    {
        header->seek(blockOffsets[13]+p*12);
        int id = header->read8();
        header->skip(1);
        Path* path = new Path(id, header->read16(), header->read16());
        qDebug("Found Path with ID %d, %d Nodes, Node Offset: %d", path->getid(), path->getNumberOfNodes(), path->getNodesOffset());
        for (int i = 0; i < path->getNumberOfNodes(); i++)
        {
            header->seek(blockOffsets[14]+i*20+path->getNodesOffset()*20);
            PathNode* pathN = new PathNode(to20(header->read16()), to20(header->read16()), header->read32(), header->read32());
            path->insertNode(*pathN);
        }
        paths.append(*path);
    }

    // Block: 15/16 Progress Paths
    for (int p = 0; p < (int)(blockSizes[15]/12); p++)
    {
        header->seek(blockOffsets[15]+p*12);
        ProgressPath* pPath = new ProgressPath(header->read16(), header->read16(), header->read16());
        qDebug("Found Progress Path with ID %d, %d Nodes, Node Offset: %d", pPath->getid(), pPath->getNumberOfNodes(), pPath->getNodesOffset());
        for (int i = 0; i < pPath->getNumberOfNodes(); i++)
        {
            header->seek(blockOffsets[16]+i*20+pPath->getNodesOffset()*20);
            ProgressPathNode* pPathN = new ProgressPathNode(to20(header->read16()), to20(header->read16()));
            pPath->insertNode(*pPathN);
        }
        progressPaths.append(*pPath);
    }


    header->close();
    delete header;


    // read bgdat
    QString bgdatfiletemp = QString("/course/course%1_bgdatL%2.bin").arg(area);
    for (int l = 0; l < 2; l++)
    {
        QString bgdatfile = bgdatfiletemp.arg(l+1);
        if (!archive->fileExists(bgdatfile)) continue;

        FileBase* bgdat = archive->openFile(bgdatfile);
        bgdat->open();
        bgdat->seek(0);
        for (;;)
        {
            if (bgdat->pos() >= bgdat->size()) break;
            quint16 id = bgdat->read16();
            if (id == 0xFFFF) break;

            BgdatObject* obj = new BgdatObject(bgdat->read16()*20, bgdat->read16()*20, bgdat->read16()*20, bgdat->read16()*20, id);
            objects[l].append(*obj);

            bgdat->skip(6);
        }
        bgdat->close();
        delete bgdat;
    }
}

Level::~Level()
{
    for (int t = 0; t < 4; t++)
    {
        if (tilesets[t])
            delete tilesets[t];
    }

    /*for (int l = 0; l < 3; l++)
    {
        for (int o = 0; o < objects[l].size(); o++)
            //
    }*/

    delete archive;
}

void Level::save()
{
    // save
}

