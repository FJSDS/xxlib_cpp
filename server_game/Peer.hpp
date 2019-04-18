// ����û����� bind �����
inline void Peer::Dispose(int const& flag) noexcept {
	if (this->Disposed()) return;

	// ���
	if (auto&& p = player_w.lock()) {
		p->peer.reset();
	}

	// ���� Dispose ( ��������� holder )
	this->BaseType::Dispose(flag);
}

// ����û����� bind �����
inline int Peer::ReceiveRequest(int const& serial, xx::Object_s&& msg) noexcept {
	switch (msg->GetTypeId()) {
	case xx::TypeId_v<PKG::Generic::Ping>: {
		// ���ó�ʱ
		this->ResetTimeoutMS(10000);
		if (auto && p = player_w.lock()) {
			p->ResetTimeoutFrameNumber();
		}

		// Я���յ������ݻط�
		pkgPong->ticks = xx::As<PKG::Generic::Ping>(msg)->ticks;
		auto r = SendResponse(serial, pkgPong);
		Flush();
		return r;
	}
	default:
		return -1;
	}
}

inline int Peer::ReceivePush(xx::Object_s&& msg) noexcept {

	// ����� bind
	if (auto && player = player_w.lock()) {
		// �Ѱ�����
		// �������ж��Ϸ�����Ϣ�����������, �����ʵ�ʱ������ʹ��, ģ������
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Fire>:
			player->recvFires.push_back(xx::As<PKG::Client_CatchFish::Fire>(msg));
			break;
		case xx::TypeId_v<PKG::Client_CatchFish::Hit>:
			player->recvHits.push_back(xx::As<PKG::Client_CatchFish::Hit>(msg));
			break;
		default:
			player->peer.reset();
			return -1;
		}
	}
	// û��� bind
	else {
		if (!isFirstPackage) return -1;
		isFirstPackage = false;

		// ��������. ֻ���� Enter
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Enter>: {
			// ���õ��������÷���ʹ��
			auto&& cfg = *catchFish->cfg;

			// �ж�Ҫ�����ĸ� scene (��ǰ��һ��, �� )
			auto&& scene = *catchFish->scene;

			// ������û��λ��. ���û�о�ֱ�ӶϿ�
			PKG::CatchFish::Sits sit;
			if (!scene.freeSits->TryPop(sit)) return -2;

			// �������������( ģ���Ѵ�db���������� )
			auto&& player = xx::Make<Player>();
			player->autoFire = false;
			player->autoIncId = 0;
			player->autoLock = false;
			player->avatar_id = 0;
			xx::MakeTo(player->cannons);
			player->coin = 100000;
			player->id = (int)sit;
			xx::MakeTo(player->nickname, "player_");
			player->nickname->append(std::to_string((int)sit));
			player->noMoney = false;
			player->scene = &scene;
			player->sit = sit;
			xx::MakeTo(player->weapons);

			// ������ʼ��̨
			auto&& cannonCfgId = 0;
			switch (cannonCfgId) {
			case 0: {
				auto&& cannonCfg = cfg.cannons->At(cannonCfgId);
				auto&& cannon = xx::Make<Cannon>();
				cannon->angle = float(cannonCfg->angle);
				xx::MakeTo(cannon->bullets);
				cannon->cfg = &*cannonCfg;
				cannon->cfgId = cannonCfgId;
				cannon->coin = 1;
				cannon->id = (int)player->cannons->len;
				cannon->player = &*player;
				cannon->pos = cfg.sitPositons->At((int)sit);
				cannon->quantity = cannonCfg->quantity;
				cannon->scene = &scene;
				cannon->fireCD = 0;
				player->cannons->Add(cannon);
				break;
			}
			// todo: more cannon types here
			default:
				return -3;
			}

			// ����ҷ�����Ӧ����
			catchFish->players.Add(player);
			scene.players->Add(player);
			scene.frameEnters.Add(&*player);

			// ��������Ӱ�
			player_w = player;
			player->peer = xx::As<Peer>(this->shared_from_this());

			// ������ҽ���֪ͨ������֡ͬ���·��¼����ϴ���
			{
				auto&& enter = xx::Make<PKG::CatchFish::Events::Enter>();
				enter->avatar_id = player->avatar_id;
				enter->cannonCfgId = player->cannons->At(0)->cfgId;
				enter->cannonCoin = player->cannons->At(0)->coin;
				enter->coin = player->coin;
				enter->nickname = player->nickname;
				enter->noMoney = player->noMoney;
				enter->playerId = player->id;
				enter->sit = player->sit;
				scene.frameEvents->events->Add(enter);
			}

			// ���ó�ʱ
			this->ResetTimeoutMS(10000);
			player->ResetTimeoutFrameNumber();
			break;
		}
		default:
			return -4;
		}
	}
	return 0;
}
