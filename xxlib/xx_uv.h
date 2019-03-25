﻿#pragma once
#include "uv.h"
#include "xx_bbuffer.h"

// todo: 简化 tcp dialer 逻辑? 参考 udp dialer ?

#define ENABLE_KCP 1

#if ENABLE_KCP
#include "ikcp.h"
#endif

namespace xx {
	struct UvTimer;
	struct UvUpdate;
	struct Uv {
		uv_loop_t uvLoop;
		BBuffer recvBB;						// shared deserialization for package receive. direct replace buf when using
		BBuffer sendBB;						// shared serialization for package send
		std::unordered_map<int, std::weak_ptr<UvUpdate>> updates;
		std::shared_ptr<UvTimer> updater;	// for live or rpc timeout check. interval: 200ms 
		int64_t nowMS = 0;					// NowSteadyEpochMS cache
		int autoId = 0;						// updater key, udp dialer port 生成: --autoId

#if ENABLE_KCP
		std::array<char, 65536> recvBuf;	// shared receive buf for kcp
		std::unordered_map<int, std::weak_ptr<UvUpdate>> udps;	// key: port
		std::shared_ptr<UvTimer> kcpUpdater;// call kcp update & udp hand shake. interval: 10ms
#endif

		Uv();
		Uv(Uv const&) = delete;
		Uv& operator=(Uv const&) = delete;

		~Uv() {
			recvBB.Reset();					// clear replaced buf.
			updater.reset();
#if ENABLE_KCP
			kcpUpdater.reset();
#endif
			int r = uv_run(&uvLoop, UV_RUN_DEFAULT);
			//Cout("~UvLooop() uv_run return ", r);
			assert(!r);
			r = uv_loop_close(&uvLoop);
			assert(!r);
			//Cout(", uv_loop_close return ", r, "\n");
		}

		inline int Run(uv_run_mode const& mode = UV_RUN_DEFAULT) noexcept {
			return uv_run(&uvLoop, mode);
		}

		inline void Stop() {
			uv_stop(&uvLoop);
		}

		template<typename T>
		static T* Alloc(void* const& ud) noexcept {
			auto p = (void**)::malloc(sizeof(void*) + sizeof(T));
			if (!p) return nullptr;
			p[0] = ud;
			return (T*)&p[1];
		}
		inline static void Free(void* const& p) noexcept {
			::free((void**)p - 1);
		}
		template<typename T>
		static T* GetSelf(void* const& p) noexcept {
			return (T*)*((void**)p - 1);
		}
		template<typename T>
		static void HandleCloseAndFree(T*& tar) noexcept {
			if (!tar) return;
			auto h = (uv_handle_t*)tar;
			tar = nullptr;
			assert(!uv_is_closing(h));
			uv_close(h, [](uv_handle_t* handle) {
				Uv::Free(handle);
			});
		}
		inline static void AllocCB(uv_handle_t* h, size_t suggested_size, uv_buf_t* buf) noexcept {
			buf->base = (char*)::malloc(suggested_size);
			buf->len = decltype(uv_buf_t::len)(suggested_size);
		}

		inline static int FillIP(sockaddr_in6& saddr, std::string& ip, bool includePort = true) noexcept {
			ip.resize(64);
			if (saddr.sin6_family == AF_INET6) {
				if (int r = uv_ip6_name(&saddr, ip.data(), ip.size())) return r;
				ip.resize(strlen(ip.data()));
				if (includePort) {
					ip.append(":");
					ip.append(std::to_string(ntohs(saddr.sin6_port)));
				}
			}
			else {
				if (int r = uv_ip4_name((sockaddr_in*)&saddr, ip.data(), ip.size())) return r;
				ip.resize(strlen(ip.data()));
				if (includePort) {
					ip.append(":");
					ip.append(std::to_string(ntohs(((sockaddr_in*)&saddr)->sin_port)));
				}
			}
			return 0;
		}
		inline static int FillIP(uv_tcp_t* stream, std::string& ip, bool includePort = true) noexcept {
			sockaddr_in6 saddr;
			int len = sizeof(saddr);
			int r = 0;
			if ((r = uv_tcp_getpeername(stream, (sockaddr*)&saddr, &len))) return r;
			return FillIP(saddr, ip, includePort);
		}
		inline static std::string ToIpPortString(sockaddr const* const& addr, bool includePort = true) noexcept {
			sockaddr_in6 a;
			memcpy(&a, addr, sizeof(addr));
			std::string ipAndPort;
			Uv::FillIP(a, ipAndPort, includePort);
			return ipAndPort;
		}
		inline static std::string ToIpPortString(sockaddr_in6 const& addr, bool includePort = true) noexcept {
			return ToIpPortString((sockaddr*)&addr, includePort);
		}
	};

	struct UvItem : std::enable_shared_from_this<UvItem> {
		Uv& uv;
		UvItem(Uv& uv) : uv(uv) {}
		virtual ~UvItem() {}
		// must be call Dispose() at all inherit class if override following funcs
		/*
			~TTTTTTTT() { this->Dispose(0); }
		*/
		virtual bool Disposed() const noexcept = 0;
		virtual void Dispose(int const& flag) noexcept = 0;	// flag == 0 : call by ~T()
	};
	using UvItem_s = std::shared_ptr<UvItem>;
	using UvItem_w = std::weak_ptr<UvItem>;

	struct UvAsync : UvItem {
		uv_async_t* uvAsync = nullptr;
		std::mutex mtx;
		std::deque<std::function<void()>> actions;
		std::function<void()> action;	// for pop store

		UvAsync(Uv& uv)
			: UvItem(uv) {
			uvAsync = Uv::Alloc<uv_async_t>(this);
			if (!uvAsync) throw - 1;
			if (int r = uv_async_init(&uv.uvLoop, uvAsync, [](uv_async_t* handle) {
				Uv::GetSelf<UvAsync>(handle)->Execute();
			})) {
				uvAsync = nullptr;
				throw r;
			}
		}
		UvAsync(UvAsync const&) = delete;
		UvAsync& operator=(UvAsync const&) = delete;
		~UvAsync() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !uvAsync;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvAsync) return;
			Uv::HandleCloseAndFree(uvAsync);
			if (flag) {
				auto holder = shared_from_this();
				actions.clear();
			}
		}
		inline int Dispatch(std::function<void()>&& action) noexcept {
			if (!uvAsync) return -1;
			{
				std::scoped_lock<std::mutex> g(mtx);
				actions.push_back(std::move(action));
			}
			return uv_async_send(uvAsync);
		}

	protected:
		inline void Execute() noexcept {
			{
				std::scoped_lock<std::mutex> g(mtx);
				action = std::move(actions.front());
				actions.pop_front();
			}
			action();
		}
	};
	using UvAsync_s = std::shared_ptr<UvAsync>;
	using UvAsync_w = std::weak_ptr<UvAsync>;

	struct UvTimer : UvItem {
		uv_timer_t* uvTimer = nullptr;
		uint64_t timeoutMS = 0;
		uint64_t repeatIntervalMS = 0;
		std::function<void()> OnFire;

		UvTimer(Uv& uv)
			: UvItem(uv) {
			uvTimer = Uv::Alloc<uv_timer_t>(this);
			if (!uvTimer) throw - 1;
			if (int r = uv_timer_init(&uv.uvLoop, uvTimer)) {
				uvTimer = nullptr;
				throw r;
			}
		}
		UvTimer(Uv& uv, uint64_t const& timeoutMS, uint64_t const& repeatIntervalMS, std::function<void()>&& onFire = nullptr)
			: UvTimer(uv) {
			if (int r = Start(timeoutMS, repeatIntervalMS, std::move(onFire))) throw r;
		}
		UvTimer(UvTimer const&) = delete;
		UvTimer& operator=(UvTimer const&) = delete;
		~UvTimer() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !uvTimer;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvTimer) return;
			Uv::HandleCloseAndFree(uvTimer);
			if (flag) {
				auto holder = shared_from_this();
				OnFire = nullptr;
			}
		}

		inline int Start(uint64_t const& timeoutMS, uint64_t const& repeatIntervalMS, std::function<void()>&& onFire = nullptr) noexcept {
			if (!uvTimer) return -1;
			this->timeoutMS = timeoutMS;
			this->repeatIntervalMS = repeatIntervalMS;
			this->OnFire = std::move(onFire);
			return uv_timer_start(uvTimer, Fire, timeoutMS, repeatIntervalMS);
		}
		inline int Restart() noexcept {
			if (!uvTimer) return -1;
			return uv_timer_start(uvTimer, Fire, timeoutMS, repeatIntervalMS);
		}
		inline int Stop() noexcept {
			if (!uvTimer) return -1;
			return uv_timer_stop(uvTimer);
		}
		inline int Again() noexcept {
			if (!uvTimer) return -1;
			return uv_timer_again(uvTimer);
		}

		// force the loop to exit early by unreferencing handles which are active
		inline void Unref() noexcept {
			uv_unref((uv_handle_t*)uvTimer);
		}

	protected:
		inline static void Fire(uv_timer_t* t) {
			auto self = Uv::GetSelf<UvTimer>(t);
			if (self->OnFire) {
				self->OnFire();
			}
		}
	};
	using UvTimer_s = std::shared_ptr<UvTimer>;
	using UvTimer_w = std::weak_ptr<UvTimer>;

	struct UvResolver;
	using UvResolver_s = std::shared_ptr<UvResolver>;
	using UvResolver_w = std::weak_ptr<UvResolver>;
	struct uv_getaddrinfo_t_ex {
		uv_getaddrinfo_t req;
		UvResolver_w resolver_w;
	};

	struct UvResolver : UvItem {
		uv_getaddrinfo_t_ex* req = nullptr;
		UvTimer_s timeouter;
		std::vector<std::string> ips;
		std::function<void()> OnFinish;
#ifdef __IPHONE_OS_VERSION_MIN_REQUIRED
		addrinfo hints;
#endif

		UvResolver(Uv& uv) noexcept
			: UvItem(uv) {
			timeouter = Make<UvTimer>(uv);
#ifdef __IPHONE_OS_VERSION_MIN_REQUIRED
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = 0;// IPPROTO_TCP;
			hints.ai_flags = AI_DEFAULT;
#endif
		}

		UvResolver(UvResolver const&) = delete;
		UvResolver& operator=(UvResolver const&) = delete;
		~UvResolver() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !timeouter;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (timeouter) {
				Cancel();
				timeouter.reset();
				if (flag) {
					auto holder = shared_from_this();
					OnFinish = nullptr;
				}
			}
		}

		inline void Cancel() {
			if (!timeouter) return;
			ips.clear();
			if (req) {
				uv_cancel((uv_req_t*)req);
				req = nullptr;
			}
			timeouter->Stop();
		}

		inline int Resolve(std::string const& domainName, uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			Cancel();
			if (timeoutMS) {
				if (int r = timeouter->Start(timeoutMS, 0, [this] {
					Cancel();
					if (OnFinish) {
						OnFinish();
					}
				})) return r;
			}
			auto req = std::make_unique<uv_getaddrinfo_t_ex>();
			req->resolver_w = As<UvResolver>(shared_from_this());
			if (int r = uv_getaddrinfo((uv_loop_t*)&uv.uvLoop, (uv_getaddrinfo_t*)&req->req, [](uv_getaddrinfo_t* req_, int status, struct addrinfo* ai) {
				auto req = std::unique_ptr<uv_getaddrinfo_t_ex>(container_of(req_, uv_getaddrinfo_t_ex, req));
				if (status) return;													// error or -4081 canceled
				auto resolver = req->resolver_w.lock();
				if (!resolver) return;
				assert(ai);

				auto& ips = resolver->ips;
				std::string s;
				do {
					s.resize(64);
					if (ai->ai_addr->sa_family == AF_INET6) {
						uv_ip6_name((sockaddr_in6*)ai->ai_addr, s.data(), s.size());
					}
					else {
						uv_ip4_name((sockaddr_in*)ai->ai_addr, s.data(), s.size());
					}
					s.resize(strlen(s.data()));

					if (std::find(ips.begin(), ips.end(), s) == ips.end()) {
						ips.push_back(std::move(s));								// known issue: ios || android will receive duplicate result
					}
					ai = ai->ai_next;
				} while (ai);
				uv_freeaddrinfo(ai);

				resolver->timeouter.reset();
				if (resolver->OnFinish) {
					resolver->OnFinish();
				}

			}, domainName.c_str(), nullptr,
#ifdef __IPHONE_OS_VERSION_MIN_REQUIRED
			(const addrinfo*)hints
#else
				nullptr
#endif
				)) return r;
			this->req = req.release();
			return 0;
		}
	};

	struct UvUpdate : UvItem {
		using UvItem::UvItem;
		int64_t timeoutMS = 0;
		int autoId = 0;

		// set timeoutMS. 0 = disable
		inline void ResetTimeoutMS(uint32_t const& ms) {
			this->timeoutMS = ms ? uv.nowMS + ms : 0;
		}

		// default timeout behavior: Dispose
		virtual void Update(int64_t const& nowMS) noexcept {
			if (timeoutMS && timeoutMS < nowMS) {
				this->Dispose();
			}
		}

		void AddToUpdates() {
			autoId = --uv.autoId;
			uv.updates[autoId] = xx::As<UvUpdate>(shared_from_this());
		};

		inline void RemoveFromUpdates() {
			uv.updates.erase(this->autoId);
		}

		inline virtual void Dispose(int const& flag = 1) noexcept override {
			RemoveFromUpdates();
		}
	};

	struct UvTcp : UvUpdate {
		uv_tcp_t* uvTcp = nullptr;

		UvTcp(Uv& uv)
			: UvUpdate(uv) {
			uvTcp = Uv::Alloc<uv_tcp_t>(this);
			if (!uvTcp) throw - 1;
			if (int r = uv_tcp_init(&uv.uvLoop, uvTcp)) {
				uvTcp = nullptr;
				throw r;
			}
		}
		~UvTcp() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !uvTcp;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvTcp) return;
			RemoveFromUpdates();
			Uv::HandleCloseAndFree(uvTcp);
		}
	};

	struct uv_write_t_ex : uv_write_t {
		uv_buf_t buf;
	};

	struct UvTcpBasePeer : UvTcp {
		Buffer buf;
		std::function<void()> OnDisconnect;
		inline virtual void Disconnect() noexcept { if (OnDisconnect) OnDisconnect(); }

		using UvTcp::UvTcp;
		UvTcpBasePeer(UvTcpBasePeer const&) = delete;
		UvTcpBasePeer& operator=(UvTcpBasePeer const&) = delete;
		~UvTcpBasePeer() { this->Dispose(0); }

		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvTcp) return;
			this->UvTcp::Dispose(flag);
			if (flag) {
				auto holder = shared_from_this();
				OnDisconnect = nullptr;
			}
		}

		// will be memcpy
		inline int Send(uint8_t const* const& buf, ssize_t const& dataLen) noexcept {
			if (!uvTcp) return -1;
			auto req = (uv_write_t_ex*)::malloc(sizeof(uv_write_t_ex) + dataLen);
			memcpy(req + 1, buf, dataLen);
			req->buf.base = (char*)(req + 1);
			req->buf.len = decltype(uv_buf_t::len)(dataLen);
			return SendReq(req);
		}

		// called by dialer or listener
		inline int ReadStart() noexcept {
			if (!uvTcp) return -1;
			return uv_read_start((uv_stream_t*)uvTcp, Uv::AllocCB, [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
				auto self = Uv::GetSelf<UvTcpBasePeer>(stream);
				auto holder = self->shared_from_this();	// hold for callback Dispose
				if (nread > 0) {
					nread = self->Unpack((uint8_t*)buf->base, (uint32_t)nread);
				}
				if (buf) ::free(buf->base);
				if (nread < 0) {
					if (!self->Disposed()) {
						self->Disconnect();
						self->Dispose();
					}
				}
			});
		}

	protected:
		// 4 byte len header. can override for write custom header format
		virtual int Unpack(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept {
			buf.AddRange(recvBuf, recvLen);
			uint32_t offset = 0;
			while (offset + 4 <= buf.len) {							// ensure header len( 4 bytes )
				auto len = buf[offset + 0] + (buf[offset + 1] << 8) + (buf[offset + 2] << 16) + (buf[offset + 3] << 24);
				if (len <= 0 /* || len > maxLimit */) return -1;	// invalid length
				if (offset + 4 + len > buf.len) break;				// not enough data

				offset += 4;
				if (int r = HandlePack(buf.buf + offset, len)) return r;
				offset += len;
			}
			buf.RemoveFront(offset);
			return 0;
		}

		// handle pack's content( except header )
		inline virtual int HandlePack(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept { return 0; };

		// launch a send request
		inline int SendReq(uv_write_t_ex* const& req) noexcept {
			if (!uvTcp) return -1;
			// todo: check send queue len ? protect?  uv_stream_get_write_queue_size((uv_stream_t*)uvTcp);
			int r = uv_write(req, (uv_stream_t*)uvTcp, &req->buf, 1, [](uv_write_t *req, int status) {
				::free(req);
			});
			if (r) Dispose();
			return r;
		}

		// fast mode. req + data 2N1, reduce malloc times.
		// reqbuf = uv_write_t_ex space + len space + data, len = data's len
		inline int SendReqAndData(uint8_t* const& reqbuf, uint32_t const& len) {
			reqbuf[sizeof(uv_write_t_ex) + 0] = uint8_t(len);		// fill package len
			reqbuf[sizeof(uv_write_t_ex) + 1] = uint8_t(len >> 8);
			reqbuf[sizeof(uv_write_t_ex) + 2] = uint8_t(len >> 16);
			reqbuf[sizeof(uv_write_t_ex) + 3] = uint8_t(len >> 24);

			auto req = (uv_write_t_ex*)reqbuf;						// fill req args
			req->buf.base = (char*)(req + 1);
			req->buf.len = decltype(uv_buf_t::len)(len + 4);
			return SendReq(req);
		}
	};

	// pack struct: [tar,] serial, data
	struct UvTcpPeer : UvTcpBasePeer {
		std::unordered_map<int, std::pair<std::function<int(Object_s&& msg)>, int64_t>> callbacks;
		int serial = 0;
		std::function<int(Object_s&& msg)> OnReceivePush;
		inline virtual int ReceivePush(Object_s&& msg) noexcept { return OnReceivePush ? OnReceivePush(std::move(msg)) : 0; };
		std::function<int(int const& serial, Object_s&& msg)> OnReceiveRequest;
		inline virtual int ReceiveRequest(int const& serial, Object_s&& msg) noexcept { return OnReceiveRequest ? OnReceiveRequest(serial, std::move(msg)) : 0; };

		UvTcpPeer(Uv& uv)
			: UvTcpBasePeer(uv) {
		}
		UvTcpPeer(UvTcpPeer const&) = delete;
		UvTcpPeer& operator=(UvTcpPeer const&) = delete;
		~UvTcpPeer() { this->Dispose(0); }

		inline int SendPush(Object_s const& data) {
			return SendPackage(data);
		}
		inline int SendResponse(int32_t const& serial, Object_s const& data, int const& tar = 0) {
			return SendPackage(data, serial);
		}
		inline int SendRequest(Object_s const& msg, std::function<int(Object_s&& msg)>&& cb, uint64_t const& timeoutMS = 0) {
			return SendRequest(msg, 0, std::move(cb), timeoutMS);
		}

		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvTcp) return;
			for (auto&& kv : callbacks) {
				kv.second.first(nullptr);
			}
			callbacks.clear();
			this->UvTcp::Dispose(flag);
			if (flag) {
				auto holder = shared_from_this();
				Disconnect();		// maybe unhold memory here
				OnDisconnect = nullptr;
				OnReceivePush = nullptr;
				OnReceiveRequest = nullptr;
			}
		}

	protected:
		virtual int HandlePack(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept override {
			auto& recvBB = uv.recvBB;
			recvBB.Reset((uint8_t*)recvBuf, recvLen);

			int serial = 0;
			if (int r = recvBB.Read(serial)) return r;
			Object_s msg;
			if (int r = recvBB.ReadRoot(msg)) return r;

			if (serial == 0) {
				return ReceivePush(std::move(msg));
			}
			else if (serial < 0) {
				return ReceiveRequest(-serial, std::move(msg));
			}
			else {
				auto&& iter = callbacks.find(serial);
				if (iter == callbacks.end()) return 0;
				auto&& a = std::move(iter->second.first);
				callbacks.erase(iter);
				return a(std::move(msg));
			}
		}

		// serial == 0: push    > 0: response    < 0: request
		inline int SendPackage(Object_s const& data, int32_t const& serial = 0, int const& tar = 0) {
			if (!uvTcp) return -1;
			auto& sendBB = uv.sendBB;
			static_assert(sizeof(uv_write_t_ex) + 4 <= 1024);
			sendBB.Reserve(1024);
			sendBB.len = sizeof(uv_write_t_ex) + 4;		// skip uv_write_t_ex + header space
			if (tar) sendBB.WriteFixed(tar);			// router package
			sendBB.Write(serial);
			sendBB.WriteRoot(data);

			auto buf = sendBB.buf;						// cut buf memory for send
			auto len = sendBB.len - sizeof(uv_write_t_ex) - 4;
			sendBB.buf = nullptr;
			sendBB.len = 0;
			sendBB.cap = 0;

			return SendReqAndData(buf, (uint32_t)len);
		}

		inline int SendRequest(Object_s const& data, int const& tar, std::function<int(Object_s&& msg)>&& cb, uint64_t const& timeoutMS = 0) {
			if (!uvTcp) return -1;
			std::pair<std::function<int(Object_s&& msg)>, int64_t> v;
			serial = (serial + 1) & 0x7FFFFFFF;			// uint circle use
			if (timeoutMS) {
				v.second = uv.nowMS + timeoutMS;
			}
			if (int r = SendPackage(data, -serial, tar)) return r;
			v.first = std::move(cb);
			callbacks[serial] = std::move(v);
			return 0;
		}

		inline virtual void Update(int64_t const& nowMS) noexcept override {
			if (!uvTcp) return;
			if (this->timeoutMS && this->timeoutMS < nowMS) {
				this->Dispose();
				return;
			}
			for (auto&& iter_ = callbacks.begin(); iter_ != callbacks.end();) {
				auto&& iter = iter_++;
				if (iter->second.second < nowMS) {
					auto&& a = std::move(iter->second.first);
					callbacks.erase(iter);
					a(nullptr);
				}
			}
		}
	};
	using UvTcpPeer_s = std::shared_ptr<UvTcpPeer>;
	using UvTcpPeer_w = std::weak_ptr<UvTcpPeer>;

	template<typename PeerType = UvTcpPeer>
	struct UvTcpListener : UvTcp {
		sockaddr_in6 addr;
		std::function<std::shared_ptr<PeerType>(Uv& uv)> OnCreatePeer;
		inline virtual std::shared_ptr<PeerType> CreatePeer() noexcept { return OnCreatePeer ? OnCreatePeer(uv) : TryMake<PeerType>(uv); }
		std::function<void(std::shared_ptr<PeerType>& peer)> OnAccept;
		inline virtual void Accept(std::shared_ptr<PeerType>& peer) noexcept { if (OnAccept) OnAccept(peer); }

		UvTcpListener(Uv& uv, std::string const& ip, int const& port, int const& backlog = 128)
			: UvTcp(uv) {
			if (ip.find(':') == std::string::npos) {
				if (uv_ip4_addr(ip.c_str(), port, (sockaddr_in*)&addr)) throw - 1;
			}
			else {
				if (uv_ip6_addr(ip.c_str(), port, &addr)) throw - 2;
			}
			if (uv_tcp_bind(uvTcp, (sockaddr*)&addr, 0)) throw - 3;

			if (uv_listen((uv_stream_t*)uvTcp, backlog, [](uv_stream_t* server, int status) {
				if (status) return;
				auto self = Uv::GetSelf<UvTcpListener<PeerType>>(server);
				auto peer = self->CreatePeer();
				if (!peer) return;
				if (uv_accept(server, (uv_stream_t*)peer->uvTcp)) return;
				if (peer->ReadStart()) return;
				peer->AddToUpdates();
				self->Accept(peer);
			})) throw - 4;
		};
		UvTcpListener(UvTcpListener const&) = delete;
		UvTcpListener& operator=(UvTcpListener const&) = delete;
		~UvTcpListener() { this->Dispose(0); }
		inline virtual bool Disposed() const noexcept override {
			return !uvTcp;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvTcp) return;
			Uv::HandleCloseAndFree(uvTcp);
			if (flag) {
				auto holder = shared_from_this();
				OnCreatePeer = nullptr;
				OnAccept = nullptr;
			}
		}
	};

	template<typename PeerType = UvTcpPeer>
	struct UvTcpDialer;

	template<typename PeerType>
	struct uv_connect_t_ex {
		uv_connect_t req;
		std::shared_ptr<PeerType> peer;
		std::weak_ptr<UvTcpDialer<PeerType>> dialer_w;
		int serial;
		int batchNumber;
		~uv_connect_t_ex();
	};

	template<typename PeerType>
	struct UvTcpDialer : UvItem {
		using ThisType = UvTcpDialer<PeerType>;
		using ReqType = uv_connect_t_ex<PeerType>;
		int serial = 0;
		std::unordered_map<int, ReqType*> reqs;
		int batchNumber = 0;
		UvTimer_s timeouter;
		std::shared_ptr<PeerType> peer;
		std::function<std::shared_ptr<PeerType>(Uv& uv)> OnCreatePeer;
		inline virtual std::shared_ptr<PeerType> CreatePeer() noexcept { return OnCreatePeer ? OnCreatePeer(uv) : TryMake<PeerType>(uv); }
		std::function<void(std::shared_ptr<PeerType>& peer)> OnAccept;
		inline virtual void Accept() noexcept { if (OnAccept) OnAccept(peer); }

		UvTcpDialer(Uv& uv)
			: UvItem(uv) {
			timeouter = Make<UvTimer>(uv);
		}
		UvTcpDialer(UvTcpDialer const&) = delete;
		UvTcpDialer& operator=(UvTcpDialer const&) = delete;
		~UvTcpDialer() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !timeouter;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!timeouter) return;
			Cancel();
			timeouter.reset();
			if (flag) {
				auto holder = shared_from_this();
				OnCreatePeer = nullptr;
				OnAccept = nullptr;
			}
		}

		// 0: free    1: dialing    2: connected		3: disposed
		inline int State() const noexcept {
			if (!timeouter) return 3;
			if (peer && !peer->Disposed()) return 2;
			if (reqs.size()) return 1;
			return 0;
		}

		inline int Dial(std::string const& ip, int const& port, uint64_t const& timeoutMS = 0, bool cleanup = true) noexcept {
			if (!timeouter) return -1;
			if (cleanup) {
				Cancel();
			}

			sockaddr_in6 addr;
			if (ip.find(':') == std::string::npos) {								// ipv4
				if (int r = uv_ip4_addr(ip.c_str(), port, (sockaddr_in*)&addr)) return r;
			}
			else {																	// ipv6
				if (int r = uv_ip6_addr(ip.c_str(), port, &addr)) return r;
			}

			if (int r = SetTimeout(timeoutMS)) return r;

			auto req = std::make_unique<ReqType>();
			req->peer = CreatePeer();
			req->dialer_w = As<ThisType>(shared_from_this());
			req->serial = ++serial;
			req->batchNumber = batchNumber;

			if (uv_tcp_connect(&req->req, req->peer->uvTcp, (sockaddr*)&addr, [](uv_connect_t* conn, int status) {
				auto req = std::unique_ptr<ReqType>(container_of(conn, ReqType, req));
				auto dialer = req->dialer_w.lock();
				if (!dialer) return;
				if (status) return;													// error or -4081 canceled
				if (dialer->batchNumber > req->batchNumber) return;
				if (dialer->peer) return;											// only fastest connected peer can survival

				if (req->peer->ReadStart()) return;
				dialer->peer = std::move(req->peer);								// connect success
				dialer->timeouter.reset();
				dialer->peer->AddToUpdates();
				dialer->Accept();
			})) return -3;

			reqs[serial] = req.release();
			return 0;
		}

		inline int Dial(std::vector<std::string> const& ips, int const& port, uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			Cancel();
			if (int r = SetTimeout(timeoutMS)) return r;
			for (auto&& ip : ips) {
				if (int r = Dial(ip, port, 0, false)) return r;
			}
			return 0;
		}
		inline int Dial(std::vector<std::pair<std::string, int>> const& ipports, uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			Cancel();
			if (int r = SetTimeout(timeoutMS)) return r;
			for (auto&& ipport : ipports) {
				if (int r = Dial(ipport.first, ipport.second, 0, false)) return r;
			}
			return 0;
		}

		inline void Cancel(bool resetPeer = true) noexcept {
			if (!timeouter) return;
			timeouter->Stop();
			if (resetPeer) {
				peer.reset();
			}
			for (auto&& kv : reqs) {
				uv_cancel((uv_req_t*)kv.second);
			}
			reqs.clear();
			serial = 0;
			++batchNumber;
		}

	protected:
		inline int SetTimeout(uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			int r = timeouter->Stop();
			if (!timeoutMS) return r;
			return timeouter->Start(timeoutMS, 0, [self_w = AsWeak<UvTcpDialer>(shared_from_this())]{
				if (auto self = self_w.lock()) {
					self->Cancel(true);
					self->Accept();
				}
				});
		}
	};

	template<typename PeerType>
	inline uv_connect_t_ex<PeerType>::~uv_connect_t_ex() {
		if (auto&& dialer = dialer_w.lock()) {
			dialer->reqs.erase(serial);
		}
	}


	struct uv_udp_send_t_ex : uv_udp_send_t {
		uv_buf_t buf;
	};

	struct UvUdp : UvUpdate {
		uv_udp_t* uvUdp = nullptr;
		sockaddr_in6 addr;
		Buffer buf;

		std::function<void()> OnDisconnect;
		inline virtual void Disconnect() noexcept { if (OnDisconnect) OnDisconnect(); }

		UvUdp(Uv& uv, std::string const& ip, int const& port, bool const& isListener)
			: UvUpdate(uv) {
			if (ip.size()) {
				if (ip.find(':') == std::string::npos) {
					if (int r = uv_ip4_addr(ip.c_str(), port, (sockaddr_in*)&addr)) throw r;
				}
				else {
					if (int r = uv_ip6_addr(ip.c_str(), port, &addr)) throw r;
				}
			}
			uvUdp = Uv::Alloc<uv_udp_t>(this);
			if (!uvUdp) throw - 2;
			if (int r = uv_udp_init(&uv.uvLoop, uvUdp)) {
				uvUdp = nullptr;
				throw r;
			}
			ScopeGuard sgUdp([this] { Uv::HandleCloseAndFree(uvUdp); });
			if (isListener) {
				if (int r = uv_udp_bind(uvUdp, (sockaddr*)&addr, UV_UDP_REUSEADDR)) throw r;
			}
			if (int r = uv_udp_recv_start(uvUdp, Uv::AllocCB, [](uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
				auto self = Uv::GetSelf<UvUdp>(handle);
				auto holder = self->shared_from_this();	// hold for callback Dispose
				if (nread > 0) {
					nread = self->Unpack((uint8_t*)buf->base, (uint32_t)nread, addr);
				}
				if (buf) ::free(buf->base);
				if (nread < 0) {
					if (!self->Disposed()) {
						self->Disconnect();
						self->Dispose();
					}
				}
			})) throw r;
			sgUdp.Cancel();
		}
		UvUdp(UvUdp const&) = delete;
		UvUdp& operator=(UvUdp const&) = delete;
		~UvUdp() { this->Dispose(0); }

		inline virtual bool Disposed() const noexcept override {
			return !uvUdp;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!uvUdp) return;
			RemoveFromUpdates();
			Uv::HandleCloseAndFree(uvUdp);
			if (flag) {
				auto handler = shared_from_this();
				Disconnect();
				OnDisconnect = nullptr;
			}
		}

		// send target: addr or this->addr
		inline int Send(uint8_t const* const& buf, ssize_t const& dataLen, sockaddr const* const& addr = nullptr) noexcept {
			if (!uvUdp) return -1;
			auto req = (uv_udp_send_t_ex*)::malloc(sizeof(uv_udp_send_t_ex) + dataLen);
			memcpy(req + 1, buf, dataLen);
			req->buf.base = (char*)(req + 1);
			req->buf.len = decltype(uv_buf_t::len)(dataLen);
			return Send(req, addr);
		}

	protected:
		virtual int Unpack(uint8_t* const& recvBuf, uint32_t const& recvLen, sockaddr const* const& addr) noexcept {
			buf.AddRange(recvBuf, recvLen);
			uint32_t offset = 0;
			while (offset + 4 <= buf.len) {							// ensure header len( 4 bytes )
				auto len = buf[offset + 0] + (buf[offset + 1] << 8) + (buf[offset + 2] << 16) + (buf[offset + 3] << 24);
				if (len <= 0 /* || len > maxLimit */) return -1;	// invalid length
				if (offset + 4 + len > buf.len) break;				// not enough data

				offset += 4;
				if (int r = HandlePack(buf.buf + offset, len, addr)) return r;
				offset += len;
			}
			buf.RemoveFront(offset);
			return 0;
		}

		virtual int HandlePack(uint8_t* const& recvBuf, uint32_t const& recvLen, sockaddr const* const& addr) { return 0; };

		// reqbuf = uv_udp_send_t_ex space + len space + data
		// len = data's len
		inline int SendReqAndData(uint8_t* const& reqbuf, uint32_t const& len, sockaddr const* const& addr = nullptr) {
			reqbuf[sizeof(uv_udp_send_t_ex) + 0] = uint8_t(len);		// fill package len
			reqbuf[sizeof(uv_udp_send_t_ex) + 1] = uint8_t(len >> 8);
			reqbuf[sizeof(uv_udp_send_t_ex) + 2] = uint8_t(len >> 16);
			reqbuf[sizeof(uv_udp_send_t_ex) + 3] = uint8_t(len >> 24);

			auto req = (uv_udp_send_t_ex*)reqbuf;						// fill req args
			req->buf.base = (char*)(req + 1);
			req->buf.len = decltype(uv_buf_t::len)(len + 4);
			return Send(req, addr);
		}

		inline int Send(uv_udp_send_t_ex* const& req, sockaddr const* const& addr = nullptr) noexcept {
			if (!uvUdp) return -1;
			// todo: check send queue len ? protect?
			int r = uv_udp_send(req, uvUdp, &req->buf, 1, addr ? addr : (sockaddr*)&this->addr, [](uv_udp_send_t *req, int status) {
				::free(req);
			});
			if (r) Dispose();
			return r;
		}
	};


#if ENABLE_KCP
	struct UvKcpPeer;
	struct UvKcpPeerOwner : UvItem {
		using UvItem::UvItem;
		inline virtual std::shared_ptr<UvKcpPeer> CreatePeer() noexcept = 0;
		inline virtual void Accept(std::shared_ptr<UvKcpPeer>& peer) noexcept = 0;
	};

	struct UvKcpUdp : UvUdp {
		using UvUdp::UvUdp;
		UvKcpPeerOwner* owner = nullptr;			// fill by owner
		int port = 0;								// fill by owner. dict's key. port > 0: listener  < 0: dialer fill by --uv.udpId
		virtual void Remove(Guid const& g) noexcept = 0;
	};

	struct UvKcpPeer : UvUpdate {
		using UvUpdate::UvUpdate;
		std::shared_ptr<UvKcpUdp> udp;				// fill by creater
		Guid guid;									// fill by creater
		int64_t createMS = 0;						// fill by creater

		ikcpcb* kcp = nullptr;
		uint32_t nextUpdateMS = 0;					// for kcp update interval control. reduce cpu usage
		int serial = 0;
		std::unordered_map<int, std::pair<std::function<int(Object_s&& msg)>, int64_t>> callbacks;
		Buffer buf;
		sockaddr_in6 addr;							// for Send. fill by owner Unpack

		std::function<void()> OnDisconnect;
		inline virtual void Disconnect() noexcept { if (OnDisconnect) OnDisconnect(); }
		// return !0 will Dispose
		std::function<int(Object_s&& msg)> OnReceivePush;
		inline virtual int ReceivePush(Object_s&& msg) noexcept { return OnReceivePush ? OnReceivePush(std::move(msg)) : 0; };
		std::function<int(int const& serial, Object_s&& msg)> OnReceiveRequest;
		inline virtual int ReceiveRequest(int const& serial, Object_s&& msg) noexcept { return OnReceiveRequest ? OnReceiveRequest(serial, std::move(msg)) : 0; };

		// 填充 udp, guid, createMS, addr 之后调用
		inline int InitKcp() {
			if (kcp) return -1;
			kcp = ikcp_create(guid, this);
			if (!kcp) return -1;
			ScopeGuard sgKcp([&] { ikcp_release(kcp); kcp = nullptr; });
			if (int r = ikcp_wndsize(kcp, 128, 128)) return r;
			if (int r = ikcp_nodelay(kcp, 1, 10, 2, 1)) return r;
			kcp->rx_minrto = 10;
			ikcp_setoutput(kcp, [](const char *inBuf, int len, ikcpcb *kcp, void *user)->int {
				auto self = ((UvKcpPeer*)user);
				return self->udp->Send((uint8_t*)inBuf, len, (sockaddr*)&self->addr);
			});
			sgKcp.Cancel();
			return 0;
		}

		virtual bool Disposed() const noexcept override {
			return !kcp;
		}
		virtual void Dispose(int const& flag = 1) noexcept override {
			if (!kcp) return;
			RemoveFromUpdates();
			ikcp_release(kcp);
			kcp = nullptr;
			udp->Remove(guid);						// remove self from container
			udp.reset();							// unbind
			for (auto&& kv : callbacks) {
				kv.second.first(nullptr);
			}
			callbacks.clear();
			if (flag) {
				auto holder = shared_from_this();
				Disconnect();
				OnDisconnect = nullptr;
				OnReceivePush = nullptr;
				OnReceiveRequest = nullptr;
			}
		}
		~UvKcpPeer() {
			this->Dispose(0);
		}

		// called by udp kcp dialer or listener
		// put data to kcp when udp receive 
		inline int Input(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept {
			if (!kcp) return -1;
			return ikcp_input(kcp, (char*)recvBuf, recvLen);
		}

		// called by udp kcp dialer or listener
		// timer call this for recv data from kcp
		inline virtual void Update(int64_t const& nowMS) noexcept override {
			if (!kcp) return;
			if (this->timeoutMS && this->timeoutMS < nowMS) {
				this->Dispose();
				return;
			}

			// rpc timeout check
			for (auto&& iter_ = callbacks.begin(); iter_ != callbacks.end();) {
				auto&& iter = iter_++;
				if (iter->second.second < nowMS) {
					auto&& a = std::move(iter->second.first);
					callbacks.erase(iter);
					a(nullptr);
				}
			}
		}

		// called by udp kcp dialer or listener
		// timer call this for recv data from kcp
		inline void UpdateKcp(int64_t const& nowMS) noexcept {
			if (!kcp) return;

			auto&& currentMS = uint32_t(nowMS - createMS);				// known issue: 超出 uint32 限制. 理论上只能持续连接几十天
			if (nextUpdateMS > currentMS) return;						// reduce cpu usage
			ikcp_update(kcp, currentMS);
			if (!kcp) return;
			nextUpdateMS = ikcp_check(kcp, currentMS);

			do {
				int recvLen = ikcp_recv(kcp, uv.recvBuf.data(), (int)uv.recvBuf.size());
				if (recvLen <= 0) break;
				if (int r = Unpack((uint8_t*)uv.recvBuf.data(), recvLen)) {
					Dispose();
					return;
				}
			} while (true);
		}


		inline int SendPush(Object_s const& data) {
			return SendPackage(data);
		}
		inline int SendResponse(int32_t const& serial, Object_s const& data) {
			return SendPackage(data, serial);
		}
		inline int SendRequest(Object_s const& msg, std::function<int(Object_s&& msg)>&& cb, uint64_t const& timeoutMS = 0) {
			return SendRequest(msg, 0, std::move(cb), timeoutMS);
		}

		// send data immediately ( no wait for more data combine send )
		inline void Flush() {
			if (!kcp) return;
			ikcp_flush(kcp);
		}

	protected:
		// 4 bytes len header. can override for custom header format.
		inline virtual int Unpack(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept {
			buf.AddRange(recvBuf, recvLen);
			uint32_t offset = 0;
			while (offset + 4 <= buf.len) {							// ensure header len( 4 bytes )
				auto len = buf[offset + 0] + (buf[offset + 1] << 8) + (buf[offset + 2] << 16) + (buf[offset + 3] << 24);
				if (len <= 0 /* || len > maxLimit */) return -1;	// invalid length
				if (offset + 4 + len > buf.len) break;				// not enough data

				offset += 4;
				if (int r = HandlePack(buf.buf + offset, len)) return r;
				offset += len;
			}
			buf.RemoveFront(offset);
			return 0;
		}

		// unpack & dispatch
		inline virtual int HandlePack(uint8_t* const& recvBuf, uint32_t const& recvLen) noexcept {
			auto& recvBB = uv.recvBB;
			recvBB.Reset((uint8_t*)recvBuf, recvLen);

			int serial = 0;
			if (int r = recvBB.Read(serial)) return r;
			Object_s msg;
			if (int r = recvBB.ReadRoot(msg)) return r;

			if (serial == 0) {
				return ReceivePush(std::move(msg));
			}
			else if (serial < 0) {
				return ReceiveRequest(-serial, std::move(msg));
			}
			else {
				auto&& iter = callbacks.find(serial);
				if (iter == callbacks.end()) return 0;
				auto&& a = std::move(iter->second.first);
				callbacks.erase(iter);
				return a(std::move(msg));
			}
		}

		// put send data into kcp. though ikcp_setoutput func send.
		inline int Send(uint8_t const* const& buf, ssize_t const& dataLen) noexcept {
			if (!kcp) return -1;
			return ikcp_send(kcp, (char*)buf, (int)dataLen);
		}

		// serial == 0: push    > 0: response    < 0: request
		inline int SendPackage(Object_s const& data, int32_t const& serial = 0, int const& tar = 0) {
			if (!kcp) return -1;
			auto& sendBB = uv.sendBB;
			sendBB.Resize(4);						// header len( 4 bytes )
			if (tar) sendBB.WriteFixed(tar);		// for router
			sendBB.Write(serial);
			sendBB.WriteRoot(data);
			auto buf = sendBB.buf;
			auto len = sendBB.len - 4;
			buf[0] = uint8_t(len);					// fill package len
			buf[1] = uint8_t(len >> 8);
			buf[2] = uint8_t(len >> 16);
			buf[3] = uint8_t(len >> 24);
			return Send(buf, sendBB.len);
		}

		inline int SendRequest(Object_s const& data, int const& tar, std::function<int(Object_s&& msg)>&& cb, uint64_t const& timeoutMS = 0) {
			if (!kcp) return -1;
			std::pair<std::function<int(Object_s&& msg)>, int64_t> v;
			serial = (serial + 1) & 0x7FFFFFFF;			// uint circle use
			if (timeoutMS) {
				v.second = uv.nowMS + timeoutMS;
			}
			if (int r = SendPackage(data, -serial, tar)) return r;
			v.first = std::move(cb);
			callbacks[serial] = std::move(v);
			return 0;
		}
	};

	struct UvKcpListenerUdp : UvKcpUdp {
		using UvKcpUdp::UvKcpUdp;
		std::unordered_map<Guid, std::weak_ptr<UvKcpPeer>> peers;
		std::unordered_map<std::string, std::pair<Guid, int64_t>> shakes;	// key: ip:port   value: guid,nowMS
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!this->uvUdp) return;
			this->UvUdp::Dispose(flag);
			for (auto&& iter = peers.begin(); iter != peers.end();) {
				if (auto&& peer = (iter++)->second.lock()) {
					peer->Dispose(flag);
				}
			}
			peers.clear();
			uv.udps.erase(port);
		}

		~UvKcpListenerUdp() {
			this->Dispose(0);
		}

		inline virtual void Update(int64_t const& nowMS) noexcept override {
			for (auto&& iter = peers.begin(); iter != peers.end();) {
				(iter++)->second.lock()->UpdateKcp(nowMS);
			}
			for (auto&& iter = shakes.begin(); iter != shakes.end();) {
				auto currIter = iter++;
				if (currIter->second.second < nowMS) {
					shakes.erase(currIter);
				}
			}
		}

		inline virtual void Remove(Guid const& g) noexcept override {
			peers.erase(g);
		}

	protected:
		inline virtual int Unpack(uint8_t* const& recvBuf, uint32_t const& recvLen, sockaddr const* const& addr) noexcept override {
			assert(port);
			// 看看是不是握手包 且这个 udp peer 的 owner 是健在的 listener
			if (recvLen == 4 && owner) {						// 握手包含有 4 字节自增序列号
				auto&& ipAndPort = Uv::ToIpPortString(addr);
				// ip_port : guid, createMS
				auto&& iter = shakes.find(ipAndPort);
				if (iter == shakes.end()) {
					iter = shakes.insert(std::make_pair(ipAndPort, std::make_pair(Guid(), uv.nowMS + 3000))).first;	// + 3000: 暂定写死握手 3 秒超时
				}
				memcpy(recvBuf + 4, &iter->second.first, 16);	// 序列号携带 guid 一起返回( 这里临时用一下 recvBuf 是安全的, 长度足够 )
				return this->Send(recvBuf, 20, addr);
			}

			// header 至少有 IKCP_OVERHEAD 字节长( kcp 头 ). 少于 IKCP_OVERHEAD 的直接忽略
			if (recvLen < IKCP_OVERHEAD) {
				return 0;
			}

			// 前 16 字节转为 Guid
			Guid g(false);
			g.Fill(recvBuf);
			std::shared_ptr<UvKcpPeer> peer;

			// 去字典中找. 如果在握手队列中发现就创建
			auto&& peerIter = peers.find(g);
			if (peerIter == peers.end()) {						// guid 未找到: 如果是 listener: 用 addr 进一步去 shakes 找
				if (!owner || owner->Disposed()) return 0;		// listener 已经没了: 忽略
				auto&& ipAndPort = Uv::ToIpPortString(addr);
				auto&& iter = shakes.find(ipAndPort);
				if (iter == shakes.end() || iter->second.first != g) return 0;	// addr 没找到 或 guid 对不上: 忽略
				shakes.erase(iter);								// 从握手队列移除
				peer = owner->CreatePeer();						// 创建 kcp peer 并填充基础数据
				if (!peer) return 0;
				peer->udp = std::move(As<UvKcpUdp>(shared_from_this()));
				peer->guid = g;
				peer->createMS = uv.nowMS;
				memcpy(&peer->addr, addr, sizeof(sockaddr_in6));// 更新 peer 的目标 ip 地址
				if (peer->InitKcp()) return 0;					// 初始化 kcp 失败直接忽略
				peers[g] = peer;								// 塞字典
				owner->Accept(peer);							// 触发 accept 回调
			}
			else {
				peer = peerIter->second.lock();
				if (!peer || peer->Disposed()) return 0;		// 如果 kcp peer 已经没了就忽略
			}

			memcpy(&peer->addr, addr, sizeof(sockaddr_in6));	// 更新 peer 的目标 ip 地址
			if (peer->Input(recvBuf, recvLen)) {
				peer->Dispose();								// peer 自己会从 peers 中移除
			}
			return 0;
		}
	};

	struct UvKcpDialerUdp : UvKcpUdp {
		using UvKcpUdp::UvKcpUdp;
		int i = 0;
		bool connected = false;
		std::weak_ptr<UvKcpPeer> peer_w;

		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!this->uvUdp) return;
			this->UvUdp::Dispose(flag);
			if (auto&& peer = peer_w.lock()) {
				peer->Dispose(flag);
			}
			uv.udps.erase(port);
		}
		~UvKcpDialerUdp() {
			this->Dispose(0);
		}
		inline virtual void Update(int64_t const& nowMS) noexcept override {
			if (connected) {
				if (auto&& peer = peer_w.lock()) {
					peer->UpdateKcp(nowMS);
				}
				else {
					Dispose();
				}
			}
			else {
				++i;
				if ((i & 0xFu) == 0) {		// 每 16 帧发送一次
					if (int r = Send((uint8_t*)&port, sizeof(port))) {
						Dispose();
					}
				}
			}
		}
		inline virtual void Remove(Guid const& g) noexcept override {
			Dispose();
		}

	protected:
		virtual int Unpack(uint8_t* const& recvBuf, uint32_t const& recvLen, sockaddr const* const& addr) noexcept override {
			assert(owner || peer_w.lock());

			// 看看是不是握手回应包
			if (recvLen == 20 && owner) {						// 握手回应包含有 4 字节自增序列号 + 16 字节 guid
				if (memcmp(recvBuf, &port, 4)) return 0;		// 序列号对不上
				auto&& p = owner->CreatePeer();
				peer_w = p;										// bind
				p->udp = std::move(As<UvKcpUdp>(shared_from_this()));
				memcpy(&p->guid, recvBuf + 4, 16);
				memcpy(&p->addr, addr, sizeof(sockaddr_in6));	// 更新 peer 的目标 ip 地址
				p->createMS = uv.nowMS;
				if (p->InitKcp()) return 0;						// 初始化 kcp 失败直接忽略
				connected = true;								// 标记为已连接
				owner->Accept(p);								// cleanup all reqs( 当前 udp 已经 bind 到 kcp 上且 kcp 被 dialer 持有, 并不会被 dispose )
				return 0;
			}

			// header 至少有 IKCP_OVERHEAD 字节长( kcp 头 ). 少于 IKCP_OVERHEAD 的直接忽略
			if (recvLen < IKCP_OVERHEAD) {
				return 0;
			}

			auto&& peer = peer_w.lock();
			if (!peer) return 0;								// 握手没完成? 忽略

			// 前 16 字节转为 Guid
			Guid g(false);
			g.Fill(recvBuf);
			if (peer->guid != g) return 0;						// guid 对不上? 忽略

			memcpy(&peer->addr, addr, sizeof(sockaddr_in6));	// 更新 peer 的目标 ip 地址
			if (peer->Input(recvBuf, recvLen)) {				// 数据输入
				peer->Dispose();								// peer 自己调用 Remove
			}
			return 0;
		}
	};

	template<typename PeerType = UvKcpPeer, typename ENABLED = std::enable_if_t<std::is_base_of_v<UvKcpPeer, PeerType>>>
	struct UvKcpListener : UvKcpPeerOwner {
		std::shared_ptr<UvKcpListenerUdp> udp;

		std::function<std::shared_ptr<PeerType>(Uv& uv)> OnCreatePeer;
		inline virtual std::shared_ptr<UvKcpPeer> CreatePeer() noexcept override { return OnCreatePeer ? OnCreatePeer(uv) : TryMake<PeerType>(uv); }
		std::function<void(std::shared_ptr<PeerType>& peer)> OnAccept;
		inline virtual void Accept(std::shared_ptr<UvKcpPeer>& peer) noexcept override { if (OnAccept) OnAccept(As<PeerType>(peer)); }

		UvKcpListener(Uv& uv, std::string const& ip, int const& port)
			: UvKcpPeerOwner(uv) {
			auto&& udps = uv.udps;
			auto iter = udps.find(port);
			if (iter != udps.end()) {
				udp = As<UvKcpListenerUdp>(iter->second.lock());
				if (udp->owner) throw - 1;			// same port listener already exists?
			}
			else {
				MakeTo(udp, uv, ip, port, true);
				udp->port = port;
				udp->owner = this;
				udps[port] = udp;
			}
			udp->owner = this;
		}
		~UvKcpListener() { this->Dispose(0); }
		virtual bool Disposed() const noexcept override {
			return !udp;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!udp) return;
			udp->owner = nullptr;								// unbind
			udp.reset();
			if (flag) {
				auto holder = shared_from_this();
				OnCreatePeer = nullptr;
				OnAccept = nullptr;
			}
		}
	};

	template<typename PeerType = UvKcpPeer, typename ENABLED = std::enable_if_t<std::is_base_of_v<UvKcpPeer, PeerType>>>
	struct UvKcpDialer : UvKcpPeerOwner {
		using UvKcpPeerOwner::UvKcpPeerOwner;
		std::unordered_map<int, std::shared_ptr<UvKcpDialerUdp>> reqs;		// key: port
		UvTimer_s timeouter;
		std::shared_ptr<PeerType> peer;

		std::function<std::shared_ptr<PeerType>(Uv& uv)> OnCreatePeer;
		inline virtual std::shared_ptr<UvKcpPeer> CreatePeer() noexcept override { return OnCreatePeer ? OnCreatePeer(uv) : TryMake<PeerType>(uv); }
		std::function<void(std::shared_ptr<PeerType>& peer)> OnAccept;

		inline virtual void Accept(std::shared_ptr<UvKcpPeer>& peer_) noexcept override {
			assert(!this->peer);
			auto&& peer = As<PeerType>(peer_);
			if (peer) {
				timeouter->Stop();
				auto&& udp = As<UvKcpDialerUdp>(peer->udp);
				udp->owner = nullptr;
				this->peer = std::move(peer);
				reqs.clear();
			}
			if (this->OnAccept) {
				this->OnAccept(this->peer);
			}
		}
		UvKcpDialer(Uv& uv)
			: UvKcpPeerOwner(uv) {
			timeouter = Make<UvTimer>(uv);
		}
		~UvKcpDialer() { this->Dispose(0); }
		virtual bool Disposed() const noexcept override {
			return !timeouter;
		}
		inline virtual void Dispose(int const& flag = 1) noexcept override {
			if (!timeouter) return;
			Cancel();
			timeouter.reset();
			if (flag) {
				auto holder = shared_from_this();
				OnCreatePeer = nullptr;
				OnAccept = nullptr;
			}
		}

		inline int Dial(std::string const& ip, int const& port, uint64_t const& timeoutMS = 0, bool cleanup = true) noexcept {
			if (!timeouter) return -1;
			if (cleanup) {
				Cancel();
			}
			if (int r = SetTimeout(timeoutMS)) return r;
			auto req = TryMake<UvKcpDialerUdp>(uv, ip, port, false);
			if (!req) return -2;
			req->owner = this;
			req->port = --uv.autoId;
			req->autoId = req->port;
			uv.udps[req->port] = req;
			reqs[req->port] = std::move(req);
			return 0;
		}

		inline int Dial(std::vector<std::string> const& ips, int const& port, uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			Cancel();
			if (int r = SetTimeout(timeoutMS)) return r;
			for (auto&& ip : ips) {
				if (int r = Dial(ip, port, 0, false)) return r;
			}
			return 0;
		}
		inline int Dial(std::vector<std::pair<std::string, int>> const& ipports, uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			Cancel();
			if (int r = SetTimeout(timeoutMS)) return r;
			for (auto&& ipport : ipports) {
				if (int r = Dial(ipport.first, ipport.second, 0, false)) return r;
			}
			return 0;
		}

		inline void Cancel(bool resetPeer = true) noexcept {
			if (!timeouter) return;
			timeouter->Stop();
			if (resetPeer) {
				peer.reset();
			}
			reqs.clear();
		}

	protected:
		inline int SetTimeout(uint64_t const& timeoutMS = 0) noexcept {
			if (!timeouter) return -1;
			int r = timeouter->Stop();
			if (!timeoutMS) return r;
			return timeouter->Start(timeoutMS, 0, [self_w = AsWeak<UvKcpDialer>(shared_from_this())]{
				if (auto self = self_w.lock()) {
					self->Cancel(true);
					self->Accept(As<UvKcpPeer>(self->peer));
				}
			});
		}
	};
#endif

	inline Uv::Uv() {
		if (int r = uv_loop_init(&uvLoop)) throw r;
		nowMS = NowSteadyEpochMS();
		MakeTo(updater, *this, 10, 200, [this] {
			nowMS = NowSteadyEpochMS();
			for (auto&& iter = updates.begin(); iter != updates.end();) {
				(iter++)->second.lock()->Update(nowMS);
			}
		});
		updater->Unref();
#if ENABLE_KCP
		MakeTo(kcpUpdater, *this, 10, 10, [this] {
			nowMS = NowSteadyEpochMS();
			for (auto&& iter = udps.begin(); iter != udps.end();) {
				(iter++)->second.lock()->Update(nowMS);
			}
		});
		kcpUpdater->Unref();
#endif
	}
}
