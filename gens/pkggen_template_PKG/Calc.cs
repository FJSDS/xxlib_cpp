﻿#pragma warning disable 0169, 0414
using TemplateLibrary;

// Calc
namespace Calc
{
    namespace CatchFish
    {
        [Desc("打击明细")]
        struct Hit
        {
            [Desc("鱼id 鱼主键1/1")]
            int fishId;

            [Desc("鱼币值")]
            long fishCoin;

            [Desc("玩家id 子弹主键1/3")]
            int playerId;

            [Desc("炮台id 子弹主键2/3")]
            int cannonId;

            [Desc("子弹id 子弹主键3/3")]
            int bulletId;

            [Desc("子弹数量( 炸弹, 强火力数量会超过 1 )")]
            int bulletCount;

            [Desc("子弹币值")]
            long bulletCoin;
        }

        [Desc("鱼被打死的明细")]
        struct Fish
        {
            int fishId;
            long fishCoin;
            int playerId;
            int cannonId;
            int bulletId;
            long bulletCoin;
        }

        [Desc("子弹打空明细")]
        struct Bullet
        {
            int playerId;
            int cannonId;
            int bulletId;
            int bulletCount;
            long bulletCoin;
        }
    }

    // todo: more games here
}
