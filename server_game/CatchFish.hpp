﻿inline CatchFish::CatchFish() {
	// 注册替代类型
	xx::BBuffer::Register<Config>(xx::TypeId_v<PKG::CatchFish::Configs::Config>);
	xx::BBuffer::Register<SpriteFrame>(xx::TypeId_v<PKG::CatchFish::Configs::SpriteFrame>);
	xx::BBuffer::Register<Physics>(xx::TypeId_v<PKG::CatchFish::Configs::Physics>);
	xx::BBuffer::Register<Scene>(xx::TypeId_v<PKG::CatchFish::Scene>);
	xx::BBuffer::Register<Player>(xx::TypeId_v<PKG::CatchFish::Player>);
	xx::BBuffer::Register<Fish>(xx::TypeId_v<PKG::CatchFish::Fish>);
	xx::BBuffer::Register<Cannon>(xx::TypeId_v<PKG::CatchFish::Cannon>);
	xx::BBuffer::Register<Bullet>(xx::TypeId_v<PKG::CatchFish::Bullet>);
	// todo: more
}

inline void CatchFish::Dispose(int const& flag) noexcept {
	if (disposed) return;

	// todo: release all
#ifdef CC_TARGET_PLATFORM
	dialer.reset();

	cc_listener.Reset();
#endif
}

inline CatchFish::~CatchFish() {
	Dispose(0);
}

inline int CatchFish::Update() noexcept {
#ifdef CC_TARGET_PLATFORM
	return dialer->Update();
#else
	return scene->Update();
#endif
}

#ifndef CC_TARGET_PLATFORM
inline int CatchFish::Init(std::string const& cfgName) noexcept {
	// 从文件加载 cfg. 出问题返回非 0
	{
		xx::BBuffer bb;
		if (int r = ReadFile(cfgName.c_str(), bb)) return r;
		if (int r = bb.ReadRoot(cfg)) return r;
	}
	// 初始化派生类的东西
	if (int r = cfg->InitCascade()) return r;

	// 场景初始化
	xx::MakeTo(scene);
	xx::MakeTo(scene->borns);
	xx::MakeTo(scene->fishs);
	xx::MakeTo(scene->freeSits);
	xx::MakeTo(scene->items);
	xx::MakeTo(scene->players);
	xx::MakeTo(scene->rnd, 123);
	xx::MakeTo(scene->stage);
	xx::MakeTo(scene->frameEvents);
	xx::MakeTo(scene->frameEvents->events);
	scene->cfg = &*cfg;
	scene->catchFish = this;

	scene->freeSits->Add(PKG::CatchFish::Sits::LeftTop
		, PKG::CatchFish::Sits::RightTop
		, PKG::CatchFish::Sits::RightBottom
		, PKG::CatchFish::Sits::LeftBottom);
#else
inline int CatchFish::Init(std::string const& ip, int const& port, std::string const& cfgName) noexcept {
	// 暂存 ip, port
	serverIp = ip;
	serverPort = port;

	assert(!cc_scene);
	// 初始化 cocos 相关
	cc_scene = cocos2d::Director::getInstance()->getRunningScene();
	cc_listener = cocos2d::EventListenerTouchAllAtOnce::create();
	cc_listener->onTouchesBegan = [](const std::vector<cocos2d::Touch*> & ts, cocos2d::Event * e) {
		cc_touchs.AddRange(ts.data(), ts.size());
	};
	cc_listener->onTouchesMoved = [](const std::vector<cocos2d::Touch*> & ts, cocos2d::Event * e) {
	};
	cc_listener->onTouchesEnded = [](const std::vector<cocos2d::Touch*> & ts, cocos2d::Event * e) {
		for (auto&& t : ts) {
			cc_touchs.Remove(t);
		}
	};
	cc_listener->onTouchesCancelled = cc_listener->onTouchesEnded;
	cocos2d::Director::getInstance()->getEventDispatcher()->addEventListenerWithSceneGraphPriority(cc_listener, cc_scene);

	// 从文件加载 cfg. 出问题返回非 0
	{
		auto&& data = cocos2d::FileUtils::getInstance()->getDataFromFile(cfgName);
		xx::BBuffer bb;
		bb.Reset(data.getBytes(), data.getSize());
		auto&& r = bb.ReadRoot(cfg);
		bb.Reset();
		if (r) return r;
	}

	// 初始化派生类的东西
	if (int r = cfg->InitCascade()) return r;

	// 存易用单例
	::catchFish = this;

	// 初始化拨号器
	xx::MakeTo(::dialer, *uv);

#endif
	return 0;
}
