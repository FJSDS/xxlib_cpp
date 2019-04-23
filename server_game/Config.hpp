﻿int Config::InitCascade(void* const& o) noexcept {

	// 按一定密度遍历组合产生所有对角斜线
	std::vector<std::pair<xx::Pos, xx::Pos>> ios_100;
	auto&& w = (ScreenWidth + 100) / 2.0f;			// todo: 100 做到 config. 半径小于 100 的鱼才适用
	auto && h = (ScreenHeight + 100) / 2.0f;
	for (int a_ = 0; a_ < 180; a_ += 10) {
		int a1 = a_;
		if (a1 < 90) {
			a1 -= 45;
		}
		else {
			a1 = a1 - 90 + 135;
		}
		auto&& p1 = xx::Rotate(xx::Pos{ 1, 0 }, a1 * (float(M_PI) / 180.0f));
		xx::Pos abs{ std::fabs(p1.x), std::fabs(p1.y) };
		if (abs.x / (abs.x + abs.y) > ScreenWidthRatio) {
			p1 = p1 * (w / abs.x);
		}
		else {
			p1 = p1 * (h / abs.y);
		}
		for (int a2 = a1 + 180 - 23; a2 < a1 + 180 - 23 + 46; a2 += 5) {
			auto&& p2 = xx::Rotate(xx::Pos{ 1, 0 }, a2 * (float(M_PI) / 180.0f));
			abs.x = std::fabs(p2.x);
			abs.y = std::fabs(p2.y);
			if (abs.x / (abs.x + abs.y) > ScreenWidthRatio) {
				p2 = p2 * (w / abs.x);
			}
			else {
				p2 = p2 * (h / abs.y);
			}
			ios_100.push_back({ p1,p2 });
		}
	}
	// 生成后为 180 根

	// 预填充一些随机曲线
	for (auto&& io : ios_100) {
		this->ways->Add(MakeCurve({ io.first, io.second }, 0.1f, { 50, 30 }));
	}

	// 预填充一些随机直线
	for (auto&& io : ios_100) {
		this->ways->Add(MakeBeeline({ io.first, io.second }));
	}

	return BaseType::InitCascade(this);
}

inline PKG::CatchFish::Way_s Config::MakeBeeline(std::pair<xx::Pos, xx::Pos> const& inOutPos) noexcept {
	auto&& way = xx::Make<PKG::CatchFish::Way>();
	xx::MakeTo(way->points);
	way->points->Add(PKG::CatchFish::WayPoint{ inOutPos.first, xx::GetAngle(inOutPos), xx::GetDistance(inOutPos) });
	way->points->Add(PKG::CatchFish::WayPoint{ inOutPos.second, 0, 0 });	// 非循环轨迹最后个点距离和角度不用计算, 也不做统计
	way->distance = way->points->At(0).distance;
	way->loop = false;
	return way;
}

inline PKG::CatchFish::Way_s Config::MakeCurve(std::pair<xx::Pos, xx::Pos> const& inOutPos, float const& xStep, xx::Pos const& ratio) noexcept {
	auto&& way = xx::Make<PKG::CatchFish::Way>();
	auto&& wps = *xx::MakeTo(way->points);

	auto&& d = xx::GetDistance(inOutPos) / ratio.x;
	wps.Reserve((int)(d / xStep));
	auto && a = xx::GetAngle(inOutPos);
	for (float x = 0; x < d; x += xStep) {
		auto&& wp = wps.Emplace();
		wp.pos = inOutPos.first + xx::Rotate(xx::Pos{ x * ratio.x, sinf(x) * ratio.y }, a);
	}
	for (size_t i = 0; i < wps.len - 1; ++i) {
		wps[i].angle = xx::GetAngle(wps[i].pos, wps[i + 1].pos);
		wps[i].distance = xx::GetDistance(wps[i].pos, wps[i + 1].pos);
	}

	way->loop = false;
	return way;
}