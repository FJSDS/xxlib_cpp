﻿#pragma warning disable 0169, 0414
using TemplateLibrary;

namespace CatchFish
{
    [Desc("场景基础配置参数 ( 主要来自 db )")]
    class SceneConfig
    {
        [Desc("游戏id")]
        int gameId;

        [Desc("级别id")]
        int levelId;

        [Desc("房间id")]
        int roomId;

        [Desc("准入金")]
        double minMoney;

        [Desc("最低炮注( coin )")]
        long minBet;

        [Desc("最高炮注( coin )")]
        long maxBet;

        [Desc("进出游戏时 money 自动兑换成 coin 要 乘除 的系数")]
        int exchangeCoinRatio;

        [Desc("子弹颗数限制 ( 分别针对每个炮台 )")]
        long maxBulletsPerCannon;
    }

    [Desc("场景")]
    class Scene
    {
        [Desc("场景基础配置参数 ( 主要来自 db )")]
        SceneConfig cfg;

        [Desc("帧编号, 每帧 + 1. 用于同步")]
        int frameNumber;

        [Desc("本地鱼生成专用随机数发生器")]
        xx.Random rnd;

        [Desc("自增id ( 从 1 开始, 用于本地鱼生成 id 填充 )")]
        int autoIncId;

        [Desc("自减id ( 从 -1 开始, 用于服务器下发鱼生成 id 填充 )")]
        int autoDecId;

        [Desc("所有鱼 ( 乱序 )")]
        List<Fish> fishss;

        [Desc("空闲座位下标( 初始时填入 Sits.LeftBottom RightBottom LeftTop RightTop )")]
        List<Sits> freeSits;

        [Desc("所有玩家")]
        List<Weak<Player>> players;
    }

    [Desc("座位列表")]
    enum Sits
    {
        [Desc("左下")]
        LeftBottom,

        [Desc("右下")]
        RightBottom,

        [Desc("右上")]
        RightTop,

        [Desc("左上")]
        LeftTop,
    }

    [Desc("炮台 ( 基类 )")]
    class Cannon
    {
        [Desc("炮倍率 ( 初始填充自 db. 范围限制为 Scene.minBet ~ maxBet )")]
        long cannonPower;

        [Desc("炮管角度 ( 每次发射时都填充一下 )")]
        float cannonAngle;

        [Desc("子弹的自增流水号")]
        int autoIncId;

        [Desc("所有子弹")]
        List<Bullet> bullets;
    }

    [Desc("玩家 ( 存在于服务 players 容器. 被 Scene.players 弱引用 )")]
    class Player
    {
        [Desc("账号id. 用于定位玩家 ( 填充自 db )")]
        int id;

        [Desc("昵称 用于客户端显示 ( 填充自 db )")]
        string nickname;

        [Desc("头像id 用于客户端显示 ( 填充自 db )")]
        int avatar_id;

        [Desc("当 Client 通过 Lobby 服务到 Game 发 Enter 时, Game 需要生成一个 token 以便 Client Enter 时传入以校验身份")]
        string token;

        [Desc("开炮等行为花掉的金币数汇总 ( 统计 )")]
        long consumeCoin;

        [Desc("当前显示游戏币数( 不代表玩家总资产 ). 当玩家进入到游戏时, 该值填充 money * exchangeCoinRatio. 玩家退出时, 做除法还原为 money.")]
        long coin;


        [Desc("所在场景")]
        Weak<Scene> scene;

        [Desc("座位")]
        Sits sit;

        [Desc("破产标识 ( 每帧开始检测一次是否破产, 是就标记之 )")]
        bool isBankRuptcy;


        [Desc("开火锁定状态")]
        bool fireLocking;

        [Desc("自动开火状态")]
        bool automating;

        [Desc("开火锁定瞄准的鱼")]
        Weak<Fish> aimFish;


        [Desc("自增id ( 从 1 开始, 用于 子弹或炮台 id 填充 )")]
        int autoIncId;

        [Desc("炮台集合 ( 常规炮 打到 钻头, 钻头飞向玩家变为 钻头炮, 覆盖在常规炮上 )")]
        List<Cannon> cannons;
    }

    [Desc("子弹 & 鱼 & 武器 的基类")]
    class MoveObject
    {
        [Desc("自增id")]
        int id;

        [Desc("位于容器时的下标 ( 用于快速交换删除 )")]
        int indexAtContainer;

        [Desc("中心点坐标")]
        xx.Pos pos;
    }

    [Desc("子弹基类")]
    class Bullet
    {
        [Desc("自增id")]
        int id;

        [Desc("位于容器时的下标 ( 用于快速交换删除 )")]
        int indexAtContainer;

        [Desc("中心点坐标")]
        xx.Pos pos;

        [Desc("每帧的直线移动坐标增量( 60fps )")]
        xx.Pos moveInc;

        [Desc("金币 / 倍率( 记录炮台开火时的 Bet 值 )")]
        long coin;
    }

    [Desc("鱼基类")]
    class Fish
    {
        [Desc("自增id")]
        int id;

        [Desc("位于容器时的下标 ( 用于快速交换删除 )")]
        int indexAtContainer;

        [Desc("中心点坐标")]
        xx.Pos pos;

        [Desc("配置id( 用来还原配置 )")]
        int cfgId;

        [Desc("移动速度系数 ( 默认为 1 )")]
        float speedScale;

        [Desc("碰撞 | 显示 体积系数 ( 默认为 1 )")]
        float sizeScale;

        //[NotSerialize, Desc("配置 ( 不参与网络传输, 派生类中根据 cfgId 去 cfgs 定位填充 )")]
        //FishConfig cfg;
    }

    [Desc("武器 ( 有一些特殊鱼死后会变做 某种武器的长相，并花一段世家飞向玩家炮台 )")]
    class Weapon
    {

    }
}
