inline int Dialer::Update() noexcept {
	lineNumber = UpdateCore(lineNumber);
	return lineNumber ? 0 : -1;
}

#include "Dialer_UpdateCore.hpp"

inline int Dialer::HandleFirstPackage() noexcept {
	switch (recvs.front()->GetTypeId()) {
	case xx::TypeId_v<PKG::CatchFish_Client::EnterSuccess>: {
		auto&& es = xx::As<PKG::CatchFish_Client::EnterSuccess>(recvs.front());

		// store players
		for (auto&& p : *es->players) {
			::catchFish->players.Add(xx::As<Player>(p));
		}

		// store scene
		::catchFish->scene = xx::As<Scene>(es->scene);

		// store current player
		player = xx::As<Player>(es->self.lock());
		token = *es->token;

		// set current player's flag
		player->isSelf = true;

		// restore scene
		::catchFish->scene->cfg = &*::catchFish->cfg;
		if (int r = ::catchFish->scene->InitCascade()) return r;

		// restore player
		for (auto&& p : ::catchFish->players) {
			if (int r = p->InitCascade(&*::catchFish->scene)) return r;
		}

		// handle finish, pop / drop.
		recvs.pop_front();
		return 0;
	}
	case xx::TypeId_v<PKG::Generic::Error>: {
		// todo: show error msg?
		return -1;
	}
	default: {
		// todo: log?
		return -1;
	}
	}
	assert(false);
}

inline int Dialer::HandlePackagesOrUpdateScene() noexcept {
	bool needUpdateScene = true;
	while (!recvs.empty()) {
		switch (recvs.front()->GetTypeId()) {
		case xx::TypeId_v<PKG::CatchFish_Client::FrameEvents>: {
			auto&& fe = xx::As<PKG::CatchFish_Client::FrameEvents>(recvs.front());
			// ��¼ / �����յ��� last frame number ���ڽ��ճ�ʱ�ж�( �ݶ� 5 �� )
			timeoutFrameNumber = fe->frameNumber + 60 * 5;
			// ����յ������ݱȱ�����̫�������
			if (timeoutFrameNumber < ::catchFish->scene->frameNumber) return -1;
			// �������֡������� server ��׷֡
			if (fe->frameNumber > ::catchFish->scene->frameNumber) {
				while (fe->frameNumber > ::catchFish->scene->frameNumber) {
					if (int r = ::catchFish->scene->Update()) return r;
				}
				needUpdateScene = false;
			}
			// ���δ����¼�����
			for (auto&& e : *fe->events) {
				if (int r = HandleEvents(e)) return r;
			}
			// ���δ���˽���¼�����
			for (auto&& e : *fe->persionalEvents) {
				if (int r = HandleEvents(e)) return r;
			}
			break;
		}
		default: {
			// todo: log?
			return -1;
		}
		}
		recvs.pop_front();
	}

	return needUpdateScene ? ::catchFish->scene->Update() : 0;
}

inline int Dialer::HandleEvents(PKG::CatchFish::Events::Event_s const& e) noexcept {
	switch (e->GetTypeId()) {
	case xx::TypeId_v<PKG::CatchFish::Events::Enter>:
		return Handle(xx::As<PKG::CatchFish::Events::Enter>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::Leave>:
		return Handle(xx::As<PKG::CatchFish::Events::Leave>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::NoMoney>:
		return Handle(xx::As<PKG::CatchFish::Events::NoMoney>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::Refund>:
		return Handle(xx::As<PKG::CatchFish::Events::Refund>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::FishDead>:
		return Handle(xx::As<PKG::CatchFish::Events::FishDead>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::PushWeapon>:
		return Handle(xx::As<PKG::CatchFish::Events::PushWeapon>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::PushFish>:
		return Handle(xx::As<PKG::CatchFish::Events::PushFish>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::OpenAutoLock>:
		return Handle(xx::As<PKG::CatchFish::Events::OpenAutoLock>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::Aim>:
		return Handle(xx::As<PKG::CatchFish::Events::Aim>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::CloseAutoLock>:
		return Handle(xx::As<PKG::CatchFish::Events::CloseAutoLock>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::OpenAutoFire>:
		return Handle(xx::As<PKG::CatchFish::Events::OpenAutoFire>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::CloseAutoFire>:
		return Handle(xx::As<PKG::CatchFish::Events::CloseAutoFire>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::Fire>:
		return Handle(xx::As<PKG::CatchFish::Events::Fire>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::CannonSwitch>:
		return Handle(xx::As<PKG::CatchFish::Events::CannonSwitch>(e));
	case xx::TypeId_v<PKG::CatchFish::Events::CannonCoinChange>:
		return Handle(xx::As<PKG::CatchFish::Events::CannonCoinChange>(e));
	default:
		// todo: log?
		assert(false);	// ����ִ�е�����
	}
	return -1;
}

inline void Dialer::Reset() noexcept {
	recvs.clear();
	player.reset();
	::catchFish->players.Clear();
	::catchFish->scene.reset();
}

inline int Dialer::Handle(PKG::CatchFish::Events::Enter_s o) noexcept {
	// �����Լ�������Ϸ����Ϣ
	if (o->playerId == player->id) return 0;

	// �������������( ģ���յ��������Է������ InitCascade )
	auto&& player = xx::Make<Player>();
	player->autoFire = false;
	player->autoIncId = 0;
	player->autoLock = false;
	player->avatar_id = o->avatar_id;
	xx::MakeTo(player->cannons);
	player->coin = o->coin;
	player->id = o->playerId;
	if (o->nickname) {
		xx::MakeTo(player->nickname, *o->nickname);
	}
	player->noMoney = o->noMoney;
	//player->scene = &*catchFish->scene;
	player->sit = o->sit;
	xx::MakeTo(player->weapons);

	// ������ʼ��̨
	switch (o->cannonCfgId) {
	case 0: {
		auto&& cannonCfg = catchFish->cfg->cannons->At(o->cannonCfgId);
		auto&& cannon = xx::Make<Cannon>();
		cannon->angle = float(cannonCfg->angle);
		xx::MakeTo(cannon->bullets);
		cannon->cfgId = o->cannonCfgId;
		cannon->coin = o->cannonCoin;
		cannon->id = (int)player->cannons->len;
		cannon->quantity = cannonCfg->quantity;
		cannon->scene = &*catchFish->scene;
		cannon->fireCD = 0;
		player->cannons->Add(cannon);
		break;
	}
			// todo: more cannon types here
	default:
		return -2;
	}

	// ����ҷ�����Ӧ����
	catchFish->players.Add(player);
	catchFish->scene->players->Add(player);

	// ��һ����ʼ��
	return player->InitCascade(&*catchFish->scene);
}

inline int Dialer::Handle(PKG::CatchFish::Events::Leave_s o) noexcept {
	// ��Ӧ���յ��Լ��뿪����Ϣ
	assert(player && player->id != o->playerId);

	// ��λ��Ŀ�����
	for (auto&& p : catchFish->players) {
		if (p->id == o->playerId) {
			// ɱ��
			catchFish->Cleanup(p);
			break;
		}
	}
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::NoMoney_s o) noexcept {
	// todo
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::Refund_s o) noexcept {

	// ��λ��Ŀ�����
	for (auto&& p : catchFish->players) {
		if (p->id == o->playerId) {
			// �˿�
			p->coin += o->coin;
			break;
		}
	}
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::FishDead_s o) noexcept {
	for (auto&& p : catchFish->players) {
		if (p->id == o->playerId) {
			// ��Ǯ
			p->coin += o->coin;
			auto&& fs = *player->scene->fishs;
			for (auto&& f : fs) {
				if (f->id == o->fishId) {
					fs[fs.len - 1]->indexAtContainer = f->indexAtContainer;
					fs.SwapRemoveAt(f->indexAtContainer);

					// todo: �ж���� o->fishDeads �����ݣ���Ҫ��һ������
					// todo: ��Ч
					break;
				}
			}
			break;
		}
	}
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::PushWeapon_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::PushFish_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::OpenAutoLock_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::Aim_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::CloseAutoLock_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::OpenAutoFire_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::CloseAutoFire_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::Fire_s o) noexcept {
	// ������Լ�����ľͺ��Ի���
	if (o->playerId == player->id) return 0;

	// ��λ��Ŀ�����
	for (auto&& p : catchFish->players) {
		if (p->id == o->playerId) {
			// ��λ��Ŀ����̨
			for (auto&& c : *p->cannons) {
				if (c->id == o->cannonId) {
					auto&& cannon = xx::As<Cannon>(c);
					// ����
					cannon->coin = o->coin;					// todo: ��������������˱�ֵ�л�֪ͨ�Ͳ���Ҫ�����ֵ��
					cannon->angle = o->tarAngle;
					(void)cannon->Fire(o->frameNumber);
					break;
				}
			}
			break;
		}
	}
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::CannonSwitch_s o) noexcept {
	return 0;
}

inline int Dialer::Handle(PKG::CatchFish::Events::CannonCoinChange_s o) noexcept {
	return 0;
}
