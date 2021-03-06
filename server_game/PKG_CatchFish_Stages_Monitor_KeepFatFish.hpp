﻿inline int PKG::CatchFish::Stages::Monitor_KeepFatFish::InitCascade(void* const& o) noexcept {
	scene = (Scene*)o;
	xx::MakeTo(counter);
	return InitCascadeCore(o);
}

inline int PKG::CatchFish::Stages::Monitor_KeepFatFish::Update(int const& ticks) noexcept {
#ifndef CC_TARGET_PLATFORM
	// 如果肥鱼条数小于限定 且 生成 cd 到了 就补鱼
	if (counter.use_count() - 1 < cfg_numFishsLimit && bornAvaliableTicks <= ticks) {
		// 生成肥鱼并放入预约容器
		auto&& f = scene->MakeRandomFish(--scene->autoDecId, cfg_coin, cfg_scaleFrom, cfg_scaleTo);
		f->counters.Add(counter);
		auto&& fb = xx::Make<PKG::CatchFish::FishBorn>();
		fb->fish = f;
		fb->beginFrameNumber = scene->frameNumber + cfg_bornDelayFrameNumber;
		scene->borns->Add(fb);

		// 生成同步事件
		{
			auto&& pf = xx::Make<PKG::CatchFish::Events::PushFish>();
			pf->born = fb;
			scene->frameEvents->events->Add(std::move(pf));
		}

		bornAvaliableTicks = ticks + cfg_bornTicksInterval;
	}
#endif
	return 0;
}
