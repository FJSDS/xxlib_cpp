﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

public static class Cannons
{
    public static void Fill(PKG.CatchFish.Configs.Config cfg)
    {
        {
            var c = new PKG.CatchFish.Configs.Cannon();
            c.id = cfg.cannons.dataLen;
            cfg.cannons.Add(c);

            c.frames = new xx.List<PKG.CatchFish.Configs.SpriteFrame>();
            c.frames.Add(GetSpriteFrame("pao_01"));
            c.frames.Add(GetSpriteFrame("zidan_01"));
            c.frames.Add(GetSpriteFrame("yuwang_1"));
            c.angle = 90;
            c.quantity = -1;
            c.muzzleLen = 100;
            c.numLimit = 500;
            c.scale = 1;
            c.shootCD = 0;
            c.zOrder = 100;
            c.radius = 15;
            c.maxRadius = 200;
            c.distance = 720 / 60;  // 一秒钟上下穿越屏幕?
        }
    }


    public static PKG.CatchFish.Configs.SpriteFrame GetSpriteFrame(string fn)
    {
        if (!Program.frames.ContainsKey(fn)) throw new System.Exception("frame not found:" + fn);
        return Program.frames[fn];
    }
}