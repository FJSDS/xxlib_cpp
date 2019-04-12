inline void Peer::Dispose(int const& flag) noexcept {
	if (this->Disposed()) return;
	// kick player, if bind
	if (auto&& p = player_w.lock()) {
		// unbind
		player_w.reset();
		p->peer.reset();

		// remove from all players cross all scene
		catchFish->players.Remove(p);

		// remove from container scene
		auto&& ps = *p->scene->players;
		assert(ps.len);
		for (size_t i = ps.len - 1; i != -1; --i) {
			if (ps[i].lock() == p) {
				ps.SwapRemoveAt(i);
			}
		}
		// �뿪֪ͨ
		{
			auto&& leave = xx::Make<PKG::CatchFish::Events::Leave>();
			leave->playerId = p->id;
			p->scene->frameEvents->events->Add(std::move(leave));
		}
	}
	this->BaseType::Dispose(flag);
}

inline int Peer::ReceiveRequest(int const& serial, xx::Object_s&& msg) noexcept {
	switch (msg->GetTypeId()) {
	case xx::TypeId_v<PKG::Generic::Ping>: {
		pkgPong->ticks = xx::As<PKG::Generic::Ping>(msg)->ticks;
		this->ResetTimeoutMS(5000);
		auto r = SendResponse(serial, pkgPong);
		Flush();
		return r;
	}
	default:
		return -1;
	}
}

inline int Peer::ReceivePush(xx::Object_s&& msg) noexcept {

	// todo: ��״̬��ʶ���Ƿ�Ϊ�װ�. ���ó�ʱ����

	if (auto && player = player_w.lock()) {
		// �Ѱ�����
		// �������ж��Ϸ�����Ϣ�����������, �����ʵ�ʱ������ʹ��, ģ������
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Fire>:
			player->recvFires.push_back(xx::As<PKG::Client_CatchFish::Fire>(msg));
			if (player->recvFires.size() > 200) return -1;			// �򵥰��ѻ����
			break;
		case xx::TypeId_v<PKG::Client_CatchFish::Hit>:
			player->recvHits.push_back(xx::As<PKG::Client_CatchFish::Hit>(msg));
			if (player->recvHits.size() > 200) return -1;			// �򵥰��ѻ����
			break;
		default:
			player->peer.reset();
			return -1;
		}
	}
	else {
		// ��������. ֻ���� Enter
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Enter>: {
			// ���õ��������÷���ʹ��
			auto&& cfg = *catchFish->cfg;

			// �ж�Ҫ�����ĸ� scene (��ǰ��һ��, �� )
			auto&& scene = *catchFish->scene;

			// ������û��λ��. ���û�о�ֱ�ӶϿ�
			PKG::CatchFish::Sits sit;
			if (!scene.freeSits->TryPop(sit)) return -1;

			// �������������( ģ���Ѵ�db���������� )
			auto&& player = xx::Make<Player>();
			player->autoFire = false;
			player->autoIncId = 0;
			player->autoLock = false;
			player->avatar_id = 0;
			xx::MakeTo(player->cannons);
			player->coin = 1000;
			//player->consumeCoin = 0;
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
				return -2;
			}

			// ����ҷ�����Ӧ����
			catchFish->players.Add(player);
			scene.players->Add(player);
			scene.frameEnters.Add(&*player);

			// ��������Ӱ�
			player_w = player;
			player->peer = xx::As<Peer>(this->shared_from_this());

			// ������ҽ���֪ͨ������֡ͬ���·��¼����ϴ���
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
			break;
		}
		default:
			return -1;
		}
	}
	return 0;
}
