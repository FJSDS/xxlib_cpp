int Peer::ReceivePush(xx::Object_s&& msg) noexcept {
	// �Ѱ�����
	if (auto && player = player_w.lock()) {
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Shoot>:
			player->recvShoots.Add(xx::As<PKG::Client_CatchFish::Shoot>(msg));
			break;
		case xx::TypeId_v<PKG::Client_CatchFish::Hit>:
			player->recvHits.Add(xx::As<PKG::Client_CatchFish::Hit>(msg));
			break;
		default:
			player->peer.reset();
			return -1;
		}
	}
	// ��������. ֻ���� Enter
	else {
		switch (msg->GetTypeId()) {
		case xx::TypeId_v<PKG::Client_CatchFish::Enter>:
			break;
		default:
			return -1;
		}
	}
	return 0;
}
