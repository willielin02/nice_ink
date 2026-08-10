#include "NiceInkLocText.h"

#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "NiceInkGameInstance.h"

// 譯文初稿由 Claude 產出（2026-08-06）；上架前建議各語母語者過目——
// 選單層短句、詞彙簡單，初稿即可完整運作。
// 欄序恆定：EN JA ZHT ZHS KO ES FR IT DE PTBR RU TR AR（存檔索引依賴、不得重排）。

namespace
{
	struct FNiLocRow
	{
		const TCHAR* S[NiLoc::NumLangs];
	};

	static const FNiLocRow GTable[static_cast<int32>(ENiLocKey::COUNT)] = {
	// YourName
	{ { TEXT("YOUR NAME"), TEXT("プレイヤー名"), TEXT("你的名字"), TEXT("你的名字"), TEXT("이름"),
		TEXT("TU NOMBRE"), TEXT("TON NOM"), TEXT("IL TUO NOME"), TEXT("DEIN NAME"),
		TEXT("SEU NOME"), TEXT("ВАШЕ ИМЯ"), TEXT("ADINIZ"), TEXT("اسمك") } },
	// ClickToType
	{ { TEXT("click to type..."), TEXT("クリックして入力…"), TEXT("點擊輸入…"), TEXT("点击输入…"), TEXT("클릭해서 입력…"),
		TEXT("haz clic para escribir…"), TEXT("clique pour écrire…"), TEXT("clicca per scrivere…"), TEXT("zum Tippen klicken…"),
		TEXT("clique para digitar…"), TEXT("нажмите, чтобы ввести…"), TEXT("yazmak için tıkla…"), TEXT("انقر للكتابة…") } },
	// HostARoom
	{ { TEXT("Host a Room"), TEXT("部屋を作る"), TEXT("開房間"), TEXT("开房间"), TEXT("방 만들기"),
		TEXT("Crear sala"), TEXT("Créer un salon"), TEXT("Crea una stanza"), TEXT("Raum erstellen"),
		TEXT("Criar sala"), TEXT("Создать комнату"), TEXT("Oda Kur"), TEXT("أنشئ غرفة") } },
	// RoomVisibility
	{ { TEXT("ROOM VISIBILITY"), TEXT("部屋の公開設定"), TEXT("房間可見性"), TEXT("房间可见性"), TEXT("방 공개 설정"),
		TEXT("VISIBILIDAD DE LA SALA"), TEXT("VISIBILITÉ DU SALON"), TEXT("VISIBILITÀ DELLA STANZA"), TEXT("RAUM-SICHTBARKEIT"),
		TEXT("VISIBILIDADE DA SALA"), TEXT("ВИДИМОСТЬ КОМНАТЫ"), TEXT("ODA GÖRÜNÜRLÜĞÜ"), TEXT("ظهور الغرفة") } },
	// InviteOnly
	{ { TEXT("Invite Only"), TEXT("招待のみ"), TEXT("邀請制"), TEXT("邀请制"), TEXT("초대 전용"),
		TEXT("Solo invitación"), TEXT("Sur invitation"), TEXT("Solo su invito"), TEXT("Nur Einladung"),
		TEXT("Só convite"), TEXT("По приглашению"), TEXT("Davetle"), TEXT("بدعوة فقط") } },
	// PublicRoom
	{ { TEXT("Public"), TEXT("公開"), TEXT("公開"), TEXT("公开"), TEXT("공개"),
		TEXT("Pública"), TEXT("Public"), TEXT("Pubblica"), TEXT("Öffentlich"),
		TEXT("Pública"), TEXT("Открытая"), TEXT("Herkese Açık"), TEXT("عامة") } },
	// JoinARoom
	{ { TEXT("Join a Room"), TEXT("部屋に入る"), TEXT("加入房間"), TEXT("加入房间"), TEXT("방 참가"),
		TEXT("Unirse a una sala"), TEXT("Rejoindre un salon"), TEXT("Entra in una stanza"), TEXT("Raum beitreten"),
		TEXT("Entrar numa sala"), TEXT("Войти в комнату"), TEXT("Odaya Katıl"), TEXT("انضم إلى غرفة") } },
	// SettingsBtn
	{ { TEXT("Settings"), TEXT("設定"), TEXT("設定"), TEXT("设置"), TEXT("설정"),
		TEXT("Ajustes"), TEXT("Options"), TEXT("Impostazioni"), TEXT("Einstellungen"),
		TEXT("Configurações"), TEXT("Настройки"), TEXT("Ayarlar"), TEXT("الإعدادات") } },
	// Quit
	{ { TEXT("Quit"), TEXT("終了"), TEXT("離開"), TEXT("退出"), TEXT("종료"),
		TEXT("Salir"), TEXT("Quitter"), TEXT("Esci"), TEXT("Beenden"),
		TEXT("Sair"), TEXT("Выход"), TEXT("Çık"), TEXT("خروج") } },
	// AskHostCode
	{ { TEXT("ask the host for their 4-letter room code"), TEXT("ホストに4文字の部屋コードを聞こう"),
		TEXT("向房主索取 4 個字母的房間碼"), TEXT("向房主索取 4 个字母的房间码"), TEXT("호스트에게 4글자 방 코드를 물어보세요"),
		TEXT("pide al anfitrión su código de sala de 4 letras"), TEXT("demande au créateur son code de salon à 4 lettres"),
		TEXT("chiedi all'host il codice stanza di 4 lettere"), TEXT("frag den Host nach dem 4-Buchstaben-Raumcode"),
		TEXT("peça ao anfitrião o código de 4 letras da sala"), TEXT("спросите у хоста 4-буквенный код комнаты"),
		TEXT("oda sahibinden 4 harfli oda kodunu isteyin"), TEXT("اطلب من المضيف رمز الغرفة المكوّن من 4 أحرف") } },
	// JoinWithCode
	{ { TEXT("Join with Code"), TEXT("コードで入る"), TEXT("輸碼加入"), TEXT("输码加入"), TEXT("코드로 참가"),
		TEXT("Unirse con código"), TEXT("Rejoindre avec code"), TEXT("Entra con codice"), TEXT("Mit Code beitreten"),
		TEXT("Entrar com código"), TEXT("Войти по коду"), TEXT("Kodla Katıl"), TEXT("انضم بالرمز") } },
	// PublicRooms
	{ { TEXT("PUBLIC ROOMS"), TEXT("公開ルーム"), TEXT("公開房間"), TEXT("公开房间"), TEXT("공개 방"),
		TEXT("SALAS PÚBLICAS"), TEXT("SALONS PUBLICS"), TEXT("STANZE PUBBLICHE"), TEXT("ÖFFENTLICHE RÄUME"),
		TEXT("SALAS PÚBLICAS"), TEXT("ОТКРЫТЫЕ КОМНАТЫ"), TEXT("AÇIK ODALAR"), TEXT("الغرف العامة") } },
	// NoPublicRooms
	{ { TEXT("no public rooms right now"), TEXT("いま公開ルームはありません"), TEXT("目前沒有公開房間"), TEXT("目前没有公开房间"),
		TEXT("지금은 공개 방이 없습니다"), TEXT("no hay salas públicas ahora"), TEXT("aucun salon public pour l'instant"),
		TEXT("nessuna stanza pubblica al momento"), TEXT("derzeit keine öffentlichen Räume"), TEXT("nenhuma sala pública no momento"),
		TEXT("сейчас нет открытых комнат"), TEXT("şu an açık oda yok"), TEXT("لا توجد غرف عامة الآن") } },
	// Refresh
	{ { TEXT("Refresh"), TEXT("更新"), TEXT("重新整理"), TEXT("刷新"), TEXT("새로고침"),
		TEXT("Actualizar"), TEXT("Actualiser"), TEXT("Aggiorna"), TEXT("Aktualisieren"),
		TEXT("Atualizar"), TEXT("Обновить"), TEXT("Yenile"), TEXT("تحديث") } },
	// Back
	{ { TEXT("Back"), TEXT("戻る"), TEXT("返回"), TEXT("返回"), TEXT("뒤로"),
		TEXT("Atrás"), TEXT("Retour"), TEXT("Indietro"), TEXT("Zurück"),
		TEXT("Voltar"), TEXT("Назад"), TEXT("Geri"), TEXT("رجوع") } },
	// WindowMode
	{ { TEXT("window mode"), TEXT("ウィンドウモード"), TEXT("視窗模式"), TEXT("窗口模式"), TEXT("창 모드"),
		TEXT("modo de ventana"), TEXT("mode fenêtre"), TEXT("modalità finestra"), TEXT("Fenstermodus"),
		TEXT("modo de janela"), TEXT("оконный режим"), TEXT("pencere modu"), TEXT("وضع النافذة") } },
	// Resolution
	{ { TEXT("resolution"), TEXT("解像度"), TEXT("解析度"), TEXT("分辨率"), TEXT("해상도"),
		TEXT("resolución"), TEXT("résolution"), TEXT("risoluzione"), TEXT("Auflösung"),
		TEXT("resolução"), TEXT("разрешение"), TEXT("çözünürlük"), TEXT("الدقة") } },
	// MouseSensitivity
	{ { TEXT("mouse sensitivity"), TEXT("マウス感度"), TEXT("滑鼠靈敏度"), TEXT("鼠标灵敏度"), TEXT("마우스 감도"),
		TEXT("sensibilidad del ratón"), TEXT("sensibilité souris"), TEXT("sensibilità mouse"), TEXT("Mausempfindlichkeit"),
		TEXT("sensibilidade do mouse"), TEXT("чувствительность мыши"), TEXT("fare hassasiyeti"), TEXT("حساسية الفأرة") } },
	// MasterVolume
	{ { TEXT("master volume"), TEXT("音量"), TEXT("主音量"), TEXT("主音量"), TEXT("전체 음량"),
		TEXT("volumen general"), TEXT("volume général"), TEXT("volume generale"), TEXT("Gesamtlautstärke"),
		TEXT("volume geral"), TEXT("общая громкость"), TEXT("ana ses"), TEXT("مستوى الصوت") } },
	// Language
	{ { TEXT("language"), TEXT("言語"), TEXT("語言"), TEXT("语言"), TEXT("언어"),
		TEXT("idioma"), TEXT("langue"), TEXT("lingua"), TEXT("Sprache"),
		TEXT("idioma"), TEXT("язык"), TEXT("dil"), TEXT("اللغة") } },
	// Fullscreen
	{ { TEXT("fullscreen"), TEXT("フルスクリーン"), TEXT("全螢幕"), TEXT("全屏"), TEXT("전체 화면"),
		TEXT("pantalla completa"), TEXT("plein écran"), TEXT("schermo intero"), TEXT("Vollbild"),
		TEXT("tela cheia"), TEXT("полный экран"), TEXT("tam ekran"), TEXT("ملء الشاشة") } },
	// Borderless
	{ { TEXT("borderless"), TEXT("ボーダーレス"), TEXT("無邊框"), TEXT("无边框"), TEXT("테두리 없음"),
		TEXT("sin bordes"), TEXT("sans bordure"), TEXT("senza bordi"), TEXT("randlos"),
		TEXT("sem bordas"), TEXT("без рамки"), TEXT("çerçevesiz"), TEXT("بلا حدود") } },
	// Windowed
	{ { TEXT("windowed"), TEXT("ウィンドウ"), TEXT("視窗"), TEXT("窗口"), TEXT("창 화면"),
		TEXT("ventana"), TEXT("fenêtré"), TEXT("finestra"), TEXT("Fenster"),
		TEXT("janela"), TEXT("в окне"), TEXT("pencere"), TEXT("نافذة") } },
	// ApplyDisplay
	{ { TEXT("Apply Display"), TEXT("画面設定を適用"), TEXT("套用顯示設定"), TEXT("应用显示设置"), TEXT("화면 설정 적용"),
		TEXT("Aplicar pantalla"), TEXT("Appliquer l'affichage"), TEXT("Applica schermo"), TEXT("Anzeige übernehmen"),
		TEXT("Aplicar exibição"), TEXT("Применить"), TEXT("Görüntüyü Uygula"), TEXT("تطبيق العرض") } },
	// Licenses
	{ { TEXT("Licenses"), TEXT("ライセンス"), TEXT("授權資訊"), TEXT("授权信息"), TEXT("라이선스"),
		TEXT("Licencias"), TEXT("Licences"), TEXT("Licenze"), TEXT("Lizenzen"),
		TEXT("Licenças"), TEXT("Лицензии"), TEXT("Lisanslar"), TEXT("التراخيص") } },
	// StatusCreating
	{ { TEXT("creating your room..."), TEXT("部屋を作成中…"), TEXT("建立房間中…"), TEXT("创建房间中…"), TEXT("방 만드는 중…"),
		TEXT("creando tu sala…"), TEXT("création du salon…"), TEXT("creazione stanza…"), TEXT("Raum wird erstellt…"),
		TEXT("criando sua sala…"), TEXT("создание комнаты…"), TEXT("oda kuruluyor…"), TEXT("جارٍ إنشاء الغرفة…") } },
	// StatusLooking
	{ { TEXT("looking for rooms..."), TEXT("部屋を検索中…"), TEXT("搜尋房間中…"), TEXT("搜索房间中…"), TEXT("방 찾는 중…"),
		TEXT("buscando salas…"), TEXT("recherche de salons…"), TEXT("ricerca stanze…"), TEXT("Räume werden gesucht…"),
		TEXT("procurando salas…"), TEXT("поиск комнат…"), TEXT("odalar aranıyor…"), TEXT("جارٍ البحث عن غرف…") } },
	// StatusJoining
	{ { TEXT("joining..."), TEXT("参加中…"), TEXT("加入中…"), TEXT("加入中…"), TEXT("참가 중…"),
		TEXT("uniéndose…"), TEXT("connexion…"), TEXT("ingresso…"), TEXT("Beitritt…"),
		TEXT("entrando…"), TEXT("вход…"), TEXT("katılınıyor…"), TEXT("جارٍ الانضمام…") } },
	// ErrEnterCode
	{ { TEXT("enter the 4-letter room code"), TEXT("4文字の部屋コードを入力してください"),
		TEXT("請輸入 4 個字母的房間碼"), TEXT("请输入 4 个字母的房间码"), TEXT("4글자 방 코드를 입력하세요"),
		TEXT("introduce el código de sala de 4 letras"), TEXT("saisis le code de salon à 4 lettres"),
		TEXT("inserisci il codice stanza di 4 lettere"), TEXT("gib den 4-Buchstaben-Raumcode ein"),
		TEXT("digite o código de 4 letras da sala"), TEXT("введите 4-буквенный код комнаты"),
		TEXT("4 harfli oda kodunu girin"), TEXT("أدخل رمز الغرفة المكوّن من 4 أحرف") } },
	// ErrNoOnline
	{ { TEXT("online services unavailable"), TEXT("オンラインサービスに接続できません"),
		TEXT("線上服務無法使用"), TEXT("在线服务不可用"), TEXT("온라인 서비스를 사용할 수 없습니다"),
		TEXT("servicios en línea no disponibles"), TEXT("services en ligne indisponibles"),
		TEXT("servizi online non disponibili"), TEXT("Online-Dienste nicht verfügbar"),
		TEXT("serviços online indisponíveis"), TEXT("онлайн-сервисы недоступны"),
		TEXT("çevrimiçi hizmetler kullanılamıyor"), TEXT("الخدمات عبر الإنترنت غير متاحة") } },
	// ErrCreateFailed
	{ { TEXT("could not create the room"), TEXT("部屋を作成できませんでした"),
		TEXT("無法建立房間"), TEXT("无法创建房间"), TEXT("방을 만들 수 없습니다"),
		TEXT("no se pudo crear la sala"), TEXT("impossible de créer le salon"),
		TEXT("impossibile creare la stanza"), TEXT("Raum konnte nicht erstellt werden"),
		TEXT("não foi possível criar a sala"), TEXT("не удалось создать комнату"),
		TEXT("oda kurulamadı"), TEXT("تعذّر إنشاء الغرفة") } },
	// ErrSearchFailed
	{ { TEXT("search failed"), TEXT("検索に失敗しました"), TEXT("搜尋失敗"), TEXT("搜索失败"), TEXT("검색에 실패했습니다"),
		TEXT("búsqueda fallida"), TEXT("échec de la recherche"), TEXT("ricerca non riuscita"), TEXT("Suche fehlgeschlagen"),
		TEXT("falha na busca"), TEXT("поиск не удался"), TEXT("arama başarısız"), TEXT("فشل البحث") } },
	// ErrNoRoomsLan
	{ { TEXT("no rooms found on this network"), TEXT("このネットワークに部屋が見つかりません"),
		TEXT("這個網路上找不到房間"), TEXT("此网络上找不到房间"), TEXT("이 네트워크에서 방을 찾을 수 없습니다"),
		TEXT("no se encontraron salas en esta red"), TEXT("aucun salon trouvé sur ce réseau"),
		TEXT("nessuna stanza trovata su questa rete"), TEXT("keine Räume in diesem Netzwerk gefunden"),
		TEXT("nenhuma sala encontrada nesta rede"), TEXT("в этой сети комнат не найдено"),
		TEXT("bu ağda oda bulunamadı"), TEXT("لم يُعثر على غرف في هذه الشبكة") } },
	// ErrRoomGone
	{ { TEXT("that room is no longer available"), TEXT("その部屋はもう存在しません"),
		TEXT("那個房間已不存在"), TEXT("那个房间已不存在"), TEXT("그 방은 더 이상 없습니다"),
		TEXT("esa sala ya no está disponible"), TEXT("ce salon n'est plus disponible"),
		TEXT("quella stanza non è più disponibile"), TEXT("dieser Raum ist nicht mehr verfügbar"),
		TEXT("essa sala não está mais disponível"), TEXT("эта комната больше недоступна"),
		TEXT("bu oda artık mevcut değil"), TEXT("لم تعد هذه الغرفة متاحة") } },
	// ErrRoomFull
	{ { TEXT("the room is full"), TEXT("部屋が満員です"), TEXT("房間已滿"), TEXT("房间已满"), TEXT("방이 가득 찼습니다"),
		TEXT("la sala está llena"), TEXT("le salon est complet"), TEXT("la stanza è piena"), TEXT("der Raum ist voll"),
		TEXT("a sala está cheia"), TEXT("комната заполнена"), TEXT("oda dolu"), TEXT("الغرفة ممتلئة") } },
	// ErrJoinFailed
	{ { TEXT("could not join the room"), TEXT("部屋に入れませんでした"),
		TEXT("無法加入房間"), TEXT("无法加入房间"), TEXT("방에 참가할 수 없습니다"),
		TEXT("no se pudo entrar en la sala"), TEXT("impossible de rejoindre le salon"),
		TEXT("impossibile entrare nella stanza"), TEXT("Beitritt zum Raum fehlgeschlagen"),
		TEXT("não foi possível entrar na sala"), TEXT("не удалось войти в комнату"),
		TEXT("odaya katılınamadı"), TEXT("تعذّر الانضمام إلى الغرفة") } },
	// ErrResolve
	{ { TEXT("could not resolve the room address"), TEXT("部屋のアドレスを解決できません"),
		TEXT("無法解析房間位址"), TEXT("无法解析房间地址"), TEXT("방 주소를 확인할 수 없습니다"),
		TEXT("no se pudo resolver la dirección de la sala"), TEXT("adresse du salon introuvable"),
		TEXT("impossibile risolvere l'indirizzo della stanza"), TEXT("Raumadresse konnte nicht aufgelöst werden"),
		TEXT("não foi possível resolver o endereço da sala"), TEXT("не удалось определить адрес комнаты"),
		TEXT("oda adresi çözümlenemedi"), TEXT("تعذّر تحديد عنوان الغرفة") } },
	// ErrNoRoomWithCode（{0}=碼）
	{ { TEXT("no room with code {0} — check the code with your host"), TEXT("コード {0} の部屋が見つかりません——ホストに確認してください"),
		TEXT("找不到房間碼 {0} 的房間——請跟房主確認"), TEXT("找不到房间码 {0} 的房间——请与房主确认"),
		TEXT("코드 {0} 의 방을 찾을 수 없습니다 — 호스트에게 확인하세요"),
		TEXT("no hay sala con el código {0} — verifícalo con el anfitrión"),
		TEXT("aucun salon avec le code {0} — vérifie avec le créateur"),
		TEXT("nessuna stanza con codice {0} — verifica con l'host"),
		TEXT("kein Raum mit Code {0} — prüfe den Code mit dem Host"),
		TEXT("nenhuma sala com o código {0} — confirme com o anfitrião"),
		TEXT("нет комнаты с кодом {0} — проверьте код у хоста"),
		TEXT("{0} kodlu oda yok — kodu oda sahibiyle kontrol edin"),
		TEXT("لا توجد غرفة بالرمز {0} — تحقّق من الرمز مع المضيف") } },
	// ErrSignIn
	{ { TEXT("Epic sign-in failed — try again"), TEXT("Epicサインインに失敗——もう一度お試しください"),
		TEXT("Epic 登入失敗——請再試一次"), TEXT("Epic 登录失败——请再试一次"), TEXT("Epic 로그인 실패 — 다시 시도하세요"),
		TEXT("fallo al iniciar sesión en Epic — inténtalo de nuevo"), TEXT("échec de connexion Epic — réessaie"),
		TEXT("accesso Epic non riuscito — riprova"), TEXT("Epic-Anmeldung fehlgeschlagen — erneut versuchen"),
		TEXT("falha no login da Epic — tente novamente"), TEXT("вход в Epic не удался — попробуйте снова"),
		TEXT("Epic girişi başarısız — tekrar deneyin"), TEXT("فشل تسجيل الدخول إلى Epic — حاول مجددًا") } },
	// LobbyCodeHint
	{ { TEXT("friends join with this code"), TEXT("友だちはこのコードで参加"), TEXT("朋友輸入這組碼加入"), TEXT("朋友输入这组码加入"),
		TEXT("친구는 이 코드로 참가"), TEXT("tus amigos entran con este código"), TEXT("tes amis rejoignent avec ce code"),
		TEXT("gli amici entrano con questo codice"), TEXT("Freunde treten mit diesem Code bei"),
		TEXT("amigos entram com este código"), TEXT("друзья входят по этому коду"),
		TEXT("arkadaşlar bu kodla katılır"), TEXT("ينضم الأصدقاء بهذا الرمز") } },
	// LobbyStart（{0}=人數）
	{ { TEXT("ENTER — start the match  ·  {0} / 6 in"), TEXT("ENTER — 試合開始  ·  {0} / 6 人"),
		TEXT("ENTER — 開始遊戲  ·  {0} / 6 人"), TEXT("ENTER — 开始游戏  ·  {0} / 6 人"),
		TEXT("ENTER — 게임 시작  ·  {0} / 6 명"), TEXT("ENTER — empezar la partida  ·  {0} / 6"),
		TEXT("ENTRÉE — lancer la partie  ·  {0} / 6"), TEXT("INVIO — inizia la partita  ·  {0} / 6"),
		TEXT("ENTER — Spiel starten  ·  {0} / 6"), TEXT("ENTER — começar a partida  ·  {0} / 6"),
		TEXT("ENTER — начать матч  ·  {0} / 6"), TEXT("ENTER — maçı başlat  ·  {0} / 6"),
		TEXT("ENTER — ابدأ المباراة  ·  {0} / 6") } },
	// LobbyWaiting（{0}=人數）
	{ { TEXT("waiting for players — {0} / 6, need at least 2"), TEXT("プレイヤー待ち — {0} / 6、最低2人"),
		TEXT("等待玩家 — {0} / 6，至少要 2 人"), TEXT("等待玩家 — {0} / 6，至少要 2 人"),
		TEXT("플레이어 대기 중 — {0} / 6, 최소 2명"), TEXT("esperando jugadores — {0} / 6, mínimo 2"),
		TEXT("en attente de joueurs — {0} / 6, minimum 2"), TEXT("in attesa di giocatori — {0} / 6, minimo 2"),
		TEXT("warte auf Spieler — {0} / 6, mindestens 2"), TEXT("aguardando jogadores — {0} / 6, mínimo 2"),
		TEXT("ожидание игроков — {0} / 6, нужно минимум 2"), TEXT("oyuncular bekleniyor — {0} / 6, en az 2"),
		TEXT("بانتظار اللاعبين — {0} / 6، على الأقل 2") } },
	// LobbyWaitingHost（{0}=人數）
	{ { TEXT("waiting for the host to start  ·  {0} / 6 in"), TEXT("ホストの開始待ち  ·  {0} / 6 人"),
		TEXT("等待房主開始  ·  {0} / 6 人"), TEXT("等待房主开始  ·  {0} / 6 人"),
		TEXT("호스트 시작 대기 중  ·  {0} / 6 명"), TEXT("esperando al anfitrión  ·  {0} / 6"),
		TEXT("en attente du créateur  ·  {0} / 6"), TEXT("in attesa dell'host  ·  {0} / 6"),
		TEXT("warte auf den Host  ·  {0} / 6"), TEXT("aguardando o anfitrião  ·  {0} / 6"),
		TEXT("ожидание хоста  ·  {0} / 6"), TEXT("oda sahibi bekleniyor  ·  {0} / 6"),
		TEXT("بانتظار المضيف  ·  {0} / 6") } },
	// Profile
	{ { TEXT("Profile"), TEXT("プロフィール"), TEXT("個人檔案"), TEXT("个人档案"), TEXT("프로필"),
		TEXT("Perfil"), TEXT("Profil"), TEXT("Profilo"), TEXT("Profil"),
		TEXT("Perfil"), TEXT("Профиль"), TEXT("Profil"), TEXT("الملف الشخصي") } },
	// UploadSelfie
	{ { TEXT("Upload Selfie"), TEXT("自撮りをアップロード"), TEXT("上傳自拍"), TEXT("上传自拍"), TEXT("셀카 업로드"),
		TEXT("Subir selfie"), TEXT("Envoyer un selfie"), TEXT("Carica un selfie"), TEXT("Selfie hochladen"),
		TEXT("Enviar selfie"), TEXT("Загрузить селфи"), TEXT("Selfie yükle"), TEXT("رفع سيلفي") } },
	// CashLabel
	{ { TEXT("CASH"), TEXT("所持金"), TEXT("現金"), TEXT("现金"), TEXT("현금"),
		TEXT("DINERO"), TEXT("ARGENT"), TEXT("CONTANTI"), TEXT("BARGELD"),
		TEXT("DINHEIRO"), TEXT("НАЛИЧНЫЕ"), TEXT("NAKİT"), TEXT("النقود") } },
	// FaceProcessing
	{ { TEXT("processing your face…"), TEXT("顔を処理中…"), TEXT("臉部處理中…"), TEXT("脸部处理中…"), TEXT("얼굴 처리 중…"),
		TEXT("procesando tu cara…"), TEXT("traitement du visage…"), TEXT("elaborazione del volto…"), TEXT("Gesicht wird verarbeitet…"),
		TEXT("processando seu rosto…"), TEXT("обработка лица…"), TEXT("yüz işleniyor…"), TEXT("جارٍ معالجة الوجه…") } },
	// FaceUpdated
	{ { TEXT("face updated!"), TEXT("顔を更新しました"), TEXT("臉已更新！"), TEXT("脸已更新！"), TEXT("얼굴이 업데이트됨"),
		TEXT("¡cara actualizada!"), TEXT("visage mis à jour !"), TEXT("volto aggiornato!"), TEXT("Gesicht aktualisiert!"),
		TEXT("rosto atualizado!"), TEXT("лицо обновлено!"), TEXT("yüz güncellendi!"), TEXT("تم تحديث الوجه!") } },
	// FaceFailed
	{ { TEXT("face processing failed"), TEXT("顔の処理に失敗しました"), TEXT("臉部處理失敗"), TEXT("脸部处理失败"), TEXT("얼굴 처리 실패"),
		TEXT("falló el procesamiento"), TEXT("échec du traitement"), TEXT("elaborazione non riuscita"), TEXT("Verarbeitung fehlgeschlagen"),
		TEXT("falha no processamento"), TEXT("ошибка обработки"), TEXT("işleme başarısız"), TEXT("فشلت المعالجة") } },
	// NotSignedIn
	{ { TEXT("not signed in yet"), TEXT("未サインイン"), TEXT("尚未登入"), TEXT("尚未登录"), TEXT("로그인되지 않음"),
		TEXT("sin sesión iniciada"), TEXT("non connecté"), TEXT("non connesso"), TEXT("nicht angemeldet"),
		TEXT("não conectado"), TEXT("не выполнен вход"), TEXT("oturum açılmadı"), TEXT("لم يتم تسجيل الدخول") } },
	// BrowHint（眉毛鐵律：2026-07-08 定案「UI 提醒請露出眉毛，否則你會沒有眉毛」）
	{ { TEXT("show your eyebrows in the photo — or your rikishi won't have any"),
		TEXT("眉毛が見える写真にしてください。隠れていると眉なしになります"),
		TEXT("自拍請露出眉毛，否則你會沒有眉毛"), TEXT("自拍请露出眉毛，否则你会没有眉毛"),
		TEXT("사진에서 눈썹이 보여야 합니다. 아니면 눈썹이 없어져요"),
		TEXT("muestra las cejas en la foto o tu luchador no tendrá"),
		TEXT("montrez vos sourcils sur la photo, sinon vous n'en aurez pas"),
		TEXT("mostra le sopracciglia nella foto o non ne avrai"),
		TEXT("Augenbrauen im Foto zeigen — sonst hast du keine"),
		TEXT("mostre as sobrancelhas na foto ou ficará sem"),
		TEXT("на фото должны быть видны брови, иначе их не будет"),
		TEXT("fotoğrafta kaşların görünsün, yoksa kaşsız kalırsın"),
		TEXT("أظهر حاجبيك في الصورة وإلا فلن يكون لديك حواجب") } },
	// ReuploadSelfie
	{ { TEXT("Re-upload Selfie"), TEXT("自撮りを再アップロード"), TEXT("重新上傳自拍"), TEXT("重新上传自拍"), TEXT("셀카 다시 업로드"),
		TEXT("Volver a subir selfie"), TEXT("Renvoyer un selfie"), TEXT("Ricarica un selfie"), TEXT("Selfie erneut hochladen"),
		TEXT("Reenviar selfie"), TEXT("Загрузить селфи заново"), TEXT("Selfie'yi yeniden yükle"), TEXT("إعادة رفع سيلفي") } },
	// SavedFaces
	{ { TEXT("YOUR FACES"), TEXT("保存した顔"), TEXT("已儲存的臉"), TEXT("已保存的脸"), TEXT("저장된 얼굴"),
		TEXT("TUS CARAS"), TEXT("TES VISAGES"), TEXT("I TUOI VOLTI"), TEXT("DEINE GESICHTER"),
		TEXT("SEUS ROSTOS"), TEXT("ВАШИ ЛИЦА"), TEXT("YÜZLERİN"), TEXT("وجوهك") } },
	// FaceGateHint（臉制閘門：無臉按 Host/Join＝導來這裡＋這一行）
	{ { TEXT("your face is your rikishi — upload a selfie to play"),
		TEXT("あなたの顔が力士になります——プレイするには自撮りをアップロードしてください"),
		TEXT("你的臉就是你的力士——要先上傳自拍才能開始遊戲"),
		TEXT("你的脸就是你的力士——要先上传自拍才能开始游戏"),
		TEXT("당신의 얼굴이 곧 당신의 리키시입니다 — 플레이하려면 셀카를 업로드하세요"),
		TEXT("tu cara es tu luchador — sube un selfie para jugar"),
		TEXT("ton visage est ton rikishi — envoie un selfie pour jouer"),
		TEXT("il tuo volto è il tuo rikishi — carica un selfie per giocare"),
		TEXT("dein Gesicht ist dein Rikishi — lade ein Selfie hoch, um zu spielen"),
		TEXT("seu rosto é seu rikishi — envie um selfie para jogar"),
		TEXT("ваше лицо — это ваш рикиси; загрузите селфи, чтобы играть"),
		TEXT("yüzün senin rikişin — oynamak için bir selfie yükle"),
		TEXT("وجهك هو المصارع الخاص بك — ارفع سيلفي لتلعب") } },
	// PrivacyHint（隱私如實聲明：本機處理、無伺服器、只有同房玩家看得到）
	{ { TEXT("your photo is processed only on your PC — we have no servers and never receive it. only players in your room see your face."),
		TEXT("写真の処理はすべてあなたのPC内で行われます。サーバーはなく、開発者が写真を受け取ることはありません。顔が見えるのは同じ部屋のプレイヤーだけです。"),
		TEXT("照片只在你的電腦上處理——我們沒有伺服器，開發者永遠不會收到你的照片。只有同房間的玩家看得到你的臉。"),
		TEXT("照片只在你的电脑上处理——我们没有服务器，开发者永远不会收到你的照片。只有同房间的玩家看得到你的脸。"),
		TEXT("사진은 당신의 PC에서만 처리됩니다. 서버가 없으며 개발자는 사진을 받지 않습니다. 같은 방의 플레이어만 당신의 얼굴을 볼 수 있습니다."),
		TEXT("tu foto se procesa solo en tu PC — no tenemos servidores y nunca la recibimos. solo los jugadores de tu sala ven tu cara."),
		TEXT("ta photo est traitée uniquement sur ton PC — nous n'avons pas de serveurs et ne la recevons jamais. seuls les joueurs de ton salon voient ton visage."),
		TEXT("la tua foto viene elaborata solo sul tuo PC — non abbiamo server e non la riceviamo mai. solo i giocatori nella tua stanza vedono il tuo volto."),
		TEXT("dein Foto wird nur auf deinem PC verarbeitet — wir haben keine Server und erhalten es nie. nur Spieler in deinem Raum sehen dein Gesicht."),
		TEXT("sua foto é processada apenas no seu PC — não temos servidores e nunca a recebemos. só os jogadores da sua sala veem seu rosto."),
		TEXT("ваше фото обрабатывается только на вашем ПК — у нас нет серверов, и мы его не получаем. ваше лицо видят только игроки в вашей комнате."),
		TEXT("fotoğrafın yalnızca kendi bilgisayarında işlenir — sunucumuz yok ve fotoğrafını asla almayız. yüzünü yalnızca odandaki oyuncular görür."),
		TEXT("تُعالَج صورتك على جهازك فقط — ليس لدينا خوادم ولن نستلمها أبدًا. لا يرى وجهك سوى اللاعبين في غرفتك.") } },
	};

	static const TCHAR* GNativeNames[NiLoc::NumLangs] = {
		TEXT("English"), TEXT("日本語"), TEXT("繁體中文"), TEXT("简体中文"), TEXT("한국어"),
		TEXT("Español"), TEXT("Français"), TEXT("Italiano"), TEXT("Deutsch"),
		TEXT("Português (BR)"), TEXT("Русский"), TEXT("Türkçe"), TEXT("العربية"),
	};

	static const TCHAR* GCultureCodes[NiLoc::NumLangs] = {
		TEXT("en"), TEXT("ja"), TEXT("zh-Hant"), TEXT("zh-Hans"), TEXT("ko"),
		TEXT("es"), TEXT("fr"), TEXT("it"), TEXT("de"),
		TEXT("pt-BR"), TEXT("ru"), TEXT("tr"), TEXT("ar"),
	};

	int32 CurrentLang(const UObject* Ctx)
	{
		if (const UNiceInkGameInstance* GI = Ctx ? UNiceInkGameInstance::Get(Ctx) : nullptr)
		{
			return FMath::Clamp(GI->GetMenuLanguage(), 0, NiLoc::NumLangs - 1);
		}
		return 0;
	}
}

namespace NiLoc
{

FString T(const UObject* Ctx, ENiLocKey Key)
{
	const int32 K = static_cast<int32>(Key);
	if (K < 0 || K >= static_cast<int32>(ENiLocKey::COUNT))
	{
		return FString();
	}
	return GTable[K].S[CurrentLang(Ctx)];
}

FString TFmt(const UObject* Ctx, ENiLocKey Key, const FString& Arg0)
{
	return T(Ctx, Key).Replace(TEXT("{0}"), *Arg0);
}

FString LangNativeName(int32 LangIndex)
{
	return GNativeNames[FMath::Clamp(LangIndex, 0, NumLangs - 1)];
}

const TCHAR* LangCultureCode(int32 LangIndex)
{
	return GCultureCodes[FMath::Clamp(LangIndex, 0, NumLangs - 1)];
}

int32 MatchLangFromCulture(const FString& Culture)
{
	// 中文要看變體（zh-TW/HK/Hant→繁、其餘 zh→簡）
	if (Culture.StartsWith(TEXT("zh")))
	{
		return (Culture.Contains(TEXT("Hant")) || Culture.Contains(TEXT("TW")) ||
			Culture.Contains(TEXT("HK")) || Culture.Contains(TEXT("MO"))) ? 2 : 3;
	}
	for (int32 i = 0; i < NumLangs; ++i)
	{
		if (Culture.StartsWith(GCultureCodes[i]))
		{
			return i;
		}
	}
	return 0; // 英文保底
}

int32 DetectDefaultLang()
{
	return MatchLangFromCulture(FInternationalization::Get().GetDefaultCulture()->GetName());
}

} // namespace NiLoc
