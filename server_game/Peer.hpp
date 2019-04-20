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
		xx::CoutTN("recv unhandled request: ", msg);
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
			xx::CoutTN("binded recv unhandled push: ", msg);
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
			auto&& o = xx::As<PKG::Client_CatchFish::Enter>(msg);

			// ���õ��������÷���ʹ��
			auto&& cfg = *catchFish->cfg;

			// �ж�Ҫ�����ĸ� scene (��ǰ��һ��, �� )
			auto&& scene = *catchFish->scene;

			// ����д������ id, �����Ŷ�λ, �߶��������߼�
			while (o->token) {
				// �� token �������
				for(auto&& p : catchFish->players) {
					if (p->token == *o->token) {
						assert(p->peer != this->shared_from_this());
						// �ߵ�ԭ������( ������: �ͻ��˺ܾ�û�յ�����, �Լ� redial, �� server ��û���ֶ��� )
						p->Kick(this->GetIP(), " reconnect");
						// ��������Ӱ�
						player_w = p;
						p->peer = xx::As<Peer>(this->shared_from_this());
						// ���뱾֡������Ϸ���б�, �Ա��·�����ͬ��
						scene.frameEnters.Add(&*p);
						// ���ó�ʱ
						this->ResetTimeoutMS(10000);
						p->ResetTimeoutFrameNumber();
						// ���سɹ�
						return 0;
					}
				}
				break;
			}

			// ������û��λ��. ���û�о�ֱ�ӶϿ�
			PKG::CatchFish::Sits sit;
			if (!scene.freeSits->TryPop(sit)) {
				xx::CoutTN("no more free sit: ", msg);
				return -2;
			}

			// �������������( ģ���Ѵ�db���������� )
			auto&& player = xx::Make<Player>();
			xx::MakeTo(player->cannons);
			player->scene = &scene;
			player->id = ++listener->playerAutoId;
			xx::Append(player->token, xx::Guid(true));
			player->sit = sit;
			player->coin = 100000;
			xx::MakeTo(player->nickname, "player_" + std::to_string(player->id));
			player->autoFire = false;
			player->autoIncId = 0;
			player->autoLock = false;
			player->avatar_id = 0;
			player->noMoney = false;
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
				xx::CoutTN("unbind recv unhandled cannon cfg id: ", msg);
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

			// �ɹ��˳�
			xx::CoutTN(GetIP(), " player enter. id = ", player->id);
			break;
		}
		default:
			xx::CoutTN("unbind recv unhandled push: ", msg);
			return -4;
		}
	}
	return 0;
}
