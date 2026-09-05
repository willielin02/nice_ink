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

	static const FNiLocRow GTable[] = {
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
	// RoomVisibility（玩家語言=「誰能進房」——「可見性」是實作詞退役）
	{ { TEXT("WHO CAN JOIN"), TEXT("参加できる人"), TEXT("誰能進房"), TEXT("谁能进房"), TEXT("참가할 수 있는 사람"),
		TEXT("QUIÉN PUEDE ENTRAR"), TEXT("QUI PEUT REJOINDRE"), TEXT("CHI PUÒ ENTRARE"), TEXT("WER KANN BEITRETEN"),
		TEXT("QUEM PODE ENTRAR"), TEXT("КТО МОЖЕТ ВОЙТИ"), TEXT("KİM KATILABİLİR"), TEXT("من يمكنه الانضمام") } },
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
		TEXT("跟房主拿 4 個字母的房號"), TEXT("跟房主拿 4 个字母的房号"), TEXT("호스트에게 4글자 방 코드를 물어보세요"),
		TEXT("pide al anfitrión su código de sala de 4 letras"), TEXT("demande au créateur son code de salon à 4 lettres"),
		TEXT("chiedi all'host il codice stanza di 4 lettere"), TEXT("frag den Host nach dem 4-Buchstaben-Raumcode"),
		TEXT("peça ao anfitrião o código de 4 letras da sala"), TEXT("спросите у хоста 4-буквенный код комнаты"),
		TEXT("oda sahibinden 4 harfli oda kodunu isteyin"), TEXT("اطلب من المضيف رمز الغرفة المكوّن من 4 أحرف") } },
	// JoinWithCode
	{ { TEXT("Join with Code"), TEXT("コードで入る"), TEXT("用房號加入"), TEXT("用房号加入"), TEXT("코드로 참가"),
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
	// Language（語言頁標題；主選單入口鈕維持「文A」＝救援語義）
	{ { TEXT("Language"), TEXT("言語"), TEXT("語言"), TEXT("语言"), TEXT("언어"),
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
		TEXT("請輸入 4 個字母的房號"), TEXT("请输入 4 个字母的房号"), TEXT("4글자 방 코드를 입력하세요"),
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
		TEXT("找不到房號 {0} 的房間——請跟房主確認"), TEXT("找不到房号 {0} 的房间——请与房主确认"),
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
	{ { TEXT("friends join with this code"), TEXT("友だちはこのコードで参加"), TEXT("朋友輸入這組房號加入"), TEXT("朋友输入这组房号加入"),
		TEXT("친구는 이 코드로 참가"), TEXT("tus amigos entran con este código"), TEXT("tes amis rejoignent avec ce code"),
		TEXT("gli amici entrano con questo codice"), TEXT("Freunde treten mit diesem Code bei"),
		TEXT("amigos entram com este código"), TEXT("друзья входят по этому коду"),
		TEXT("arkadaşlar bu kodla katılır"), TEXT("ينضم الأصدقاء بهذا الرمز") } },
	// LobbyStart（{0}=「目前/上限」——2026-08-13 房主人數設定：上限隨房走，
	// 寫死「/ 6」退役、{0} 自帶完整計數）
	{ { TEXT("ENTER — start the match  ·  {0} in"), TEXT("ENTER — 試合開始  ·  {0} 人"),
		TEXT("ENTER — 開始遊戲  ·  {0} 人"), TEXT("ENTER — 开始游戏  ·  {0} 人"),
		TEXT("ENTER — 게임 시작  ·  {0} 명"), TEXT("ENTER — empezar la partida  ·  {0}"),
		TEXT("ENTRÉE — lancer la partie  ·  {0}"), TEXT("INVIO — inizia la partita  ·  {0}"),
		TEXT("ENTER — Spiel starten  ·  {0}"), TEXT("ENTER — começar a partida  ·  {0}"),
		TEXT("ENTER — начать матч  ·  {0}"), TEXT("ENTER — maçı başlat  ·  {0}"),
		TEXT("ENTER — ابدأ المباراة  ·  {0}") } },
	// LobbyWaiting（{0}=「目前/房間人數」；開局門檻=遊戲規則 4 人）
	{ { TEXT("waiting for players — {0}, need at least 4"), TEXT("プレイヤー待ち — {0}、最低4人"),
		TEXT("等待玩家 — {0}，至少要 4 人"), TEXT("等待玩家 — {0}，至少要 4 人"),
		TEXT("플레이어 대기 중 — {0}, 최소 4명"), TEXT("esperando jugadores — {0}, mínimo 4"),
		TEXT("en attente de joueurs — {0}, minimum 4"), TEXT("in attesa di giocatori — {0}, minimo 4"),
		TEXT("warte auf Spieler — {0}, mindestens 4"), TEXT("aguardando jogadores — {0}, mínimo 4"),
		TEXT("ожидание игроков — {0}, нужно минимум 4"), TEXT("oyuncular bekleniyor — {0}, en az 4"),
		TEXT("بانتظار اللاعبين — {0}، على الأقل 4") } },
	// LobbyWaitingHost（{0}=「目前/上限」）
	{ { TEXT("waiting for the host to start  ·  {0} in"), TEXT("ホストの開始待ち  ·  {0} 人"),
		TEXT("等待房主開始  ·  {0} 人"), TEXT("等待房主开始  ·  {0} 人"),
		TEXT("호스트 시작 대기 중  ·  {0} 명"), TEXT("esperando al anfitrión  ·  {0}"),
		TEXT("en attente du créateur  ·  {0}"), TEXT("in attesa dell'host  ·  {0}"),
		TEXT("warte auf den Host  ·  {0}"), TEXT("aguardando o anfitrião  ·  {0}"),
		TEXT("ожидание хоста  ·  {0}"), TEXT("oda sahibi bekleniyor  ·  {0}"),
		TEXT("بانتظار المضيف  ·  {0}") } },
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
	// CancelBtn
	{ { TEXT("Cancel"), TEXT("キャンセル"), TEXT("取消"), TEXT("取消"), TEXT("취소"),
		TEXT("Cancelar"), TEXT("Annuler"), TEXT("Annulla"), TEXT("Abbrechen"),
		TEXT("Cancelar"), TEXT("Отмена"), TEXT("İptal"), TEXT("إلغاء") } },
	// FaceGateDoor（門檻寫在門口：無臉時主卡預告，取代「按了才彈走」的驚訝）
	{ { TEXT("upload a selfie in your profile before you host or join"),
		TEXT("部屋を作る/入る前に、プロフィールで自撮りをアップロードしてください"),
		TEXT("開房或加入前，先到個人檔案上傳自拍"),
		TEXT("开房或加入前，先到个人档案上传自拍"),
		TEXT("방을 만들거나 참가하기 전에 프로필에서 셀카를 업로드하세요"),
		TEXT("sube un selfie en tu perfil antes de crear o unirte"),
		TEXT("ajoute un selfie dans ton profil avant de créer ou rejoindre"),
		TEXT("carica un selfie nel profilo prima di creare o entrare"),
		TEXT("lade zuerst ein Selfie in deinem Profil hoch"),
		TEXT("envie uma selfie no seu perfil antes de criar ou entrar"),
		TEXT("сначала загрузите селфи в профиле"),
		TEXT("önce profilinden bir özçekim yükle"),
		TEXT("حمّل صورة سيلفي في ملفك الشخصي قبل الإنشاء أو الانضمام") } },
	// FaceProcessingDoor
	{ { TEXT("your selfie is processing — you can play once it's done"),
		TEXT("自撮りを処理中——完了したら遊べます"),
		TEXT("自拍處理中——完成後就能開玩"),
		TEXT("自拍处理中——完成后就能开玩"),
		TEXT("셀카 처리 중 — 완료되면 시작할 수 있어요"),
		TEXT("procesando tu selfie — podrás jugar al terminar"),
		TEXT("selfie en cours de traitement — tu pourras jouer ensuite"),
		TEXT("selfie in elaborazione — potrai giocare appena finito"),
		TEXT("Selfie wird verarbeitet — danach kannst du spielen"),
		TEXT("processando sua selfie — você poderá jogar em seguida"),
		TEXT("селфи обрабатывается — скоро можно играть"),
		TEXT("özçekim işleniyor — bitince oynayabilirsin"),
		TEXT("جارٍ معالجة صورتك — يمكنك اللعب بعد الانتهاء") } },
	// SignInBrowserNote（行動句：說「接下來會怎樣」不只報狀態）
	{ { TEXT("signs in to Epic automatically when you host or join (opens a browser)"),
		TEXT("部屋を作る/入るときに自動でEpicにサインインします（ブラウザが開きます）"),
		TEXT("開房或加入時會自動登入 Epic（會開啟瀏覽器）"),
		TEXT("开房或加入时会自动登录 Epic（会打开浏览器）"),
		TEXT("방을 만들거나 참가할 때 자동으로 Epic에 로그인합니다(브라우저가 열립니다)"),
		TEXT("inicia sesión en Epic automáticamente al crear o unirte (abre el navegador)"),
		TEXT("connexion Epic automatique en créant ou rejoignant (ouvre le navigateur)"),
		TEXT("accesso Epic automatico creando o entrando (apre il browser)"),
		TEXT("meldet dich beim Erstellen/Beitreten automatisch bei Epic an (öffnet den Browser)"),
		TEXT("entra na Epic automaticamente ao criar ou entrar (abre o navegador)"),
		TEXT("вход в Epic выполнится автоматически при создании или входе (откроется браузер)"),
		TEXT("oda kurarken/katılırken Epic'e otomatik giriş yapılır (tarayıcı açılır)"),
		TEXT("يتم تسجيل الدخول إلى Epic تلقائيًا عند الإنشاء أو الانضمام (يفتح المتصفح)") } },
	// InviteOnlyDesc
	{ { TEXT("only people with the room code can join"),
		TEXT("部屋コードを知っている人だけ入れます"),
		TEXT("只有拿到房號的人能加入"),
		TEXT("只有拿到房号的人能加入"),
		TEXT("방 코드를 아는 사람만 참가할 수 있어요"),
		TEXT("solo puede unirse quien tenga el código"),
		TEXT("seuls ceux qui ont le code peuvent rejoindre"),
		TEXT("può entrare solo chi ha il codice"),
		TEXT("nur wer den Raumcode hat, kann beitreten"),
		TEXT("só entra quem tiver o código da sala"),
		TEXT("войти могут только те, у кого есть код"),
		TEXT("sadece oda kodu olanlar katılabilir"),
		TEXT("لا ينضم إلا من يملك رمز الغرفة") } },
	// PublicDesc
	{ { TEXT("anyone can join from the public room list"),
		TEXT("公開ルーム一覧から誰でも入れます"),
		TEXT("任何人都能從公開房間列表加入"),
		TEXT("任何人都能从公开房间列表加入"),
		TEXT("누구나 공개 방 목록에서 참가할 수 있어요"),
		TEXT("cualquiera puede unirse desde la lista pública"),
		TEXT("tout le monde peut rejoindre via la liste publique"),
		TEXT("chiunque può entrare dalla lista pubblica"),
		TEXT("jeder kann über die öffentliche Liste beitreten"),
		TEXT("qualquer um pode entrar pela lista pública"),
		TEXT("любой может войти из списка открытых комнат"),
		TEXT("herkes açık oda listesinden katılabilir"),
		TEXT("يمكن لأي شخص الانضمام من قائمة الغرف العامة") } },
	// ConfirmQuit（破壞性動作二段確認）
	{ { TEXT("press again to quit"), TEXT("もう一度押すと終了"), TEXT("再按一次離開"), TEXT("再按一次退出"), TEXT("한 번 더 누르면 종료"),
		TEXT("pulsa otra vez para salir"), TEXT("appuie encore pour quitter"), TEXT("premi di nuovo per uscire"), TEXT("erneut drücken zum Beenden"),
		TEXT("pressione de novo para sair"), TEXT("нажмите ещё раз для выхода"), TEXT("çıkmak için tekrar bas"), TEXT("اضغط مرة أخرى للخروج") } },
	// TypeCodeHint
	{ { TEXT("type the code on your keyboard"), TEXT("キーボードでコードを入力"), TEXT("直接用鍵盤輸入"), TEXT("直接用键盘输入"), TEXT("키보드로 코드를 입력하세요"),
		TEXT("escribe el código con el teclado"), TEXT("tape le code au clavier"), TEXT("digita il codice con la tastiera"), TEXT("Code über die Tastatur eingeben"),
		TEXT("digite o código no teclado"), TEXT("введите код с клавиатуры"), TEXT("kodu klavyeyle yaz"), TEXT("اكتب الرمز بلوحة المفاتيح") } },
	// NameFallbackNote（隨機保底名以 hint 呈現＝「還沒取名」狀態可見）
	{ { TEXT("random name — click above to set yours"),
		TEXT("ランダム名です——クリックして自分の名前に"),
		TEXT("這是隨機名——點上面改成你的名字"),
		TEXT("这是随机名——点上面改成你的名字"),
		TEXT("임의의 이름이에요 — 클릭해서 바꿔 보세요"),
		TEXT("nombre aleatorio — haz clic arriba para cambiarlo"),
		TEXT("nom aléatoire — clique au-dessus pour le changer"),
		TEXT("nome casuale — clicca sopra per cambiarlo"),
		TEXT("Zufallsname — oben klicken zum Ändern"),
		TEXT("nome aleatório — clique acima para mudar"),
		TEXT("случайное имя — нажмите выше, чтобы изменить"),
		TEXT("rastgele ad — değiştirmek için yukarı tıkla"),
		TEXT("اسم عشوائي — انقر أعلاه لتغييره") } },
	// NameSavedNote
	{ { TEXT("saved"), TEXT("保存しました"), TEXT("已儲存"), TEXT("已保存"), TEXT("저장됨"),
		TEXT("guardado"), TEXT("enregistré"), TEXT("salvato"), TEXT("gespeichert"),
		TEXT("salvo"), TEXT("сохранено"), TEXT("kaydedildi"), TEXT("تم الحفظ") } },
	// InstantNote（設定：立即生效組的說明＝與「要按套用」組分家）
	{ { TEXT("changes take effect immediately"), TEXT("変更は即時反映されます"), TEXT("調整立即生效"), TEXT("调整立即生效"), TEXT("즉시 적용됩니다"),
		TEXT("los cambios se aplican al instante"), TEXT("prise d'effet immédiate"), TEXT("le modifiche sono immediate"), TEXT("Änderungen gelten sofort"),
		TEXT("as mudanças valem na hora"), TEXT("изменения применяются сразу"), TEXT("değişiklikler anında uygulanır"), TEXT("تسري التغييرات فوراً") } },
	// BorderlessResNote（無邊框＝解析度旋鈕停用的原因說明）
	{ { TEXT("borderless always uses your desktop resolution"),
		TEXT("ボーダーレスはデスクトップ解像度で固定です"),
		TEXT("無邊框模式固定使用桌面解析度"),
		TEXT("无边框模式固定使用桌面分辨率"),
		TEXT("테두리 없음은 바탕화면 해상도를 사용해요"),
		TEXT("sin bordes usa la resolución del escritorio"),
		TEXT("sans bordure utilise la résolution du bureau"),
		TEXT("senza bordi usa la risoluzione del desktop"),
		TEXT("randlos nutzt immer die Desktop-Auflösung"),
		TEXT("sem bordas usa a resolução da área de trabalho"),
		TEXT("без рамки использует разрешение рабочего стола"),
		TEXT("çerçevesiz, masaüstü çözünürlüğünü kullanır"),
		TEXT("وضع بلا حدود يستخدم دقة سطح المكتب") } },
	// UnappliedDiscardWarn（未套用就返回＝二段確認，改動不再無聲蒸發）
	{ { TEXT("display settings not applied — press back again to discard"),
		TEXT("画面設定が未適用です——もう一度戻るで破棄"),
		TEXT("顯示設定尚未套用——再按一次返回放棄變更"),
		TEXT("显示设置尚未应用——再按一次返回放弃更改"),
		TEXT("화면 설정이 적용되지 않았어요 — 한 번 더 누르면 취소됩니다"),
		TEXT("pantalla sin aplicar — pulsa atrás otra vez para descartar"),
		TEXT("affichage non appliqué — retour encore pour abandonner"),
		TEXT("schermo non applicato — premi indietro di nuovo per annullare"),
		TEXT("Anzeige nicht übernommen — erneut Zurück zum Verwerfen"),
		TEXT("exibição não aplicada — voltar de novo para descartar"),
		TEXT("настройки экрана не применены — нажмите назад ещё раз, чтобы отменить"),
		TEXT("görüntü uygulanmadı — vazgeçmek için tekrar geri bas"),
		TEXT("لم تُطبَّق إعدادات العرض — اضغط رجوع مرة أخرى للتجاهل") } },
	// RenderScale（效能旋鈕；玩家語言=「畫質」——「渲染比例」是實作詞退役）
	{ { TEXT("graphics quality"), TEXT("画質"), TEXT("畫質"), TEXT("画质"), TEXT("화질"),
		TEXT("calidad gráfica"), TEXT("qualité graphique"), TEXT("qualità grafica"), TEXT("Grafikqualität"),
		TEXT("qualidade gráfica"), TEXT("качество графики"), TEXT("grafik kalitesi"), TEXT("جودة الرسوم") } },
	// CreateYourRikishi（首啟創角頁標題）
	{ { TEXT("Create Your Rikishi"), TEXT("あなたの力士を作ろう"), TEXT("創建你的力士"), TEXT("创建你的力士"),
		TEXT("당신의 리키시 만들기"), TEXT("Crea tu luchador"), TEXT("Crée ton rikishi"),
		TEXT("Crea il tuo rikishi"), TEXT("Erstelle deinen Rikishi"), TEXT("Crie seu rikishi"),
		TEXT("Создайте своего рикиси"), TEXT("Rikişini Oluştur"), TEXT("أنشئ المصارع الخاص بك") } },
	// MaxPlayersLabel（建房頁「房間人數」段標——房主直接決定這房幾個人；
	// 「上限/下限」概念退役；譯文=Claude 初稿待母語校對）
	{ { TEXT("PLAYERS"), TEXT("人数"), TEXT("房間人數"), TEXT("房间人数"), TEXT("인원"),
		TEXT("JUGADORES"), TEXT("JOUEURS"), TEXT("GIOCATORI"), TEXT("SPIELER"),
		TEXT("JOGADORES"), TEXT("ИГРОКИ"), TEXT("OYUNCU"), TEXT("عدد اللاعبين") } },
	// RoomNameLabel（公開房房名＝徵人啟事；譯文=Claude 初稿待母語校對）
	{ { TEXT("ROOM NAME"), TEXT("部屋の名前"), TEXT("房間名稱"), TEXT("房间名称"), TEXT("방 이름"),
		TEXT("NOMBRE DE LA SALA"), TEXT("NOM DU SALON"), TEXT("NOME DELLA STANZA"), TEXT("RAUMNAME"),
		TEXT("NOME DA SALA"), TEXT("НАЗВАНИЕ КОМНАТЫ"), TEXT("ODA ADI"), TEXT("اسم الغرفة") } },
	// RoomNameHint（房名輸入框 hint：讓別人知道你在找怎樣的玩家）
	{ { TEXT("tell strangers who you're looking for"), TEXT("どんな人を探してるか伝えよう"),
		TEXT("讓別人知道你在找怎樣的玩家"), TEXT("让别人知道你在找怎样的玩家"),
		TEXT("어떤 플레이어를 찾는지 알려주세요"), TEXT("di qué jugadores buscas"),
		TEXT("dis quels joueurs tu cherches"), TEXT("di' che giocatori cerchi"),
		TEXT("sag, wen du suchst"), TEXT("diga que jogadores procura"),
		TEXT("скажите, кого вы ищете"), TEXT("kimi aradığını söyle"),
		TEXT("أخبر الآخرين عمن تبحث") } },
	// OpenPublicShortcut（空列表捷徑：死路變轉化）
	{ { TEXT("host one yourself"), TEXT("自分で部屋を作る"), TEXT("自己開一間"), TEXT("自己开一间"),
		TEXT("직접 방 만들기"), TEXT("crea una tú mismo"), TEXT("crée le tien"), TEXT("creane una tu"),
		TEXT("mach selbst eins auf"), TEXT("crie uma você mesmo"), TEXT("создайте свою"),
		TEXT("kendin kur"), TEXT("أنشئ واحدة بنفسك") } },
	// AllLanguages（加入頁語言過濾）
	{ { TEXT("all languages"), TEXT("すべての言語"), TEXT("全部語言"), TEXT("全部语言"),
		TEXT("모든 언어"), TEXT("todos los idiomas"), TEXT("toutes les langues"), TEXT("tutte le lingue"),
		TEXT("alle Sprachen"), TEXT("todos os idiomas"), TEXT("все языки"),
		TEXT("tüm diller"), TEXT("كل اللغات") } },
	// FrameLimit（設定：幀率上限——玩家語言用「上限」，t.MaxFPS 是實作詞）
	// 設定列標籤一律小寫（同頁 window mode／graphics quality 的既有慣例）
	{ { TEXT("frame rate cap"), TEXT("フレームレート上限"), TEXT("幀率上限"), TEXT("帧率上限"),
		TEXT("프레임 제한"), TEXT("límite de fps"), TEXT("limite de fps"), TEXT("limite fps"),
		TEXT("bildraten-limit"), TEXT("limite de fps"), TEXT("ограничение fps"),
		TEXT("kare hızı sınırı"), TEXT("حد معدل الإطارات") } },
	// VSyncLabel（設定：垂直同步）
	{ { TEXT("v-sync"), TEXT("垂直同期"), TEXT("垂直同步"), TEXT("垂直同步"),
		TEXT("수직 동기화"), TEXT("sincronización vertical"), TEXT("synchro verticale"), TEXT("sincronia verticale"),
		TEXT("v-sync"), TEXT("sincronização vertical"), TEXT("вертикальная синхр."),
		TEXT("dikey eşitleme"), TEXT("المزامنة الرأسية") } },
	// Unlimited（幀率上限的「無上限」值）
	{ { TEXT("unlimited"), TEXT("無制限"), TEXT("無上限"), TEXT("无上限"),
		TEXT("무제한"), TEXT("sin límite"), TEXT("illimité"), TEXT("illimitato"),
		TEXT("unbegrenzt"), TEXT("sem limite"), TEXT("без ограничения"),
		TEXT("sınırsız"), TEXT("بلا حد") } },
	// OptionOn
	{ { TEXT("on"), TEXT("オン"), TEXT("開"), TEXT("开"),
		TEXT("켜기"), TEXT("activado"), TEXT("activé"), TEXT("attivo"),
		TEXT("an"), TEXT("ligado"), TEXT("вкл"),
		TEXT("açık"), TEXT("تشغيل") } },
	// OptionOff
	{ { TEXT("off"), TEXT("オフ"), TEXT("關"), TEXT("关"),
		TEXT("끄기"), TEXT("desactivado"), TEXT("désactivé"), TEXT("spento"),
		TEXT("aus"), TEXT("desligado"), TEXT("выкл"),
		TEXT("kapalı"), TEXT("إيقاف") } },
	// PhaseDrawing
	{ { TEXT("DRAWING"), TEXT("彫り中"), TEXT("作畫中"), TEXT("作画中"),
		TEXT("작업 중"), TEXT("TATUANDO"), TEXT("EN COURS"), TEXT("IN CORSO"), TEXT("AM WERK"),
		TEXT("TATUANDO"), TEXT("ТАТУИРУЕМ"), TEXT("ÇALIŞMA"), TEXT("الوشم جارٍ") } },
	// SubjectAsleep
	{ { TEXT("{0} is asleep"), TEXT("{0} は寝ている"), TEXT("{0} 睡著了"), TEXT("{0} 睡着了"),
		TEXT("{0} 님이 잠들었다"), TEXT("{0} está dormido"), TEXT("{0} dort"), TEXT("{0} dorme"), TEXT("{0} schläft"),
		TEXT("{0} está dormindo"), TEXT("{0} спит"), TEXT("{0} uyuyor"), TEXT("{0} نائم") } },
	// NeedleStencil
	{ { TEXT("STENCIL"), TEXT("下書き"), TEXT("打稿"), TEXT("打稿"),
		TEXT("밑그림"), TEXT("PLANTILLA"), TEXT("CALQUE"), TEXT("STENCIL"), TEXT("VORZEICHNUNG"),
		TEXT("ESBOÇO"), TEXT("ЭСКИЗ"), TEXT("ŞABLON"), TEXT("تخطيط") } },
	// NeedleLiner
	{ { TEXT("LINER"), TEXT("筋彫り"), TEXT("割線"), TEXT("割线"),
		TEXT("라이너"), TEXT("LÍNEA"), TEXT("LIGNE"), TEXT("LINEA"), TEXT("LINIE"),
		TEXT("LINHA"), TEXT("КОНТУР"), TEXT("ÇİZGİ"), TEXT("خط") } },
	// NeedleShader
	{ { TEXT("SHADER"), TEXT("ぼかし"), TEXT("打霧"), TEXT("打雾"),
		TEXT("셰이딩"), TEXT("SOMBRA"), TEXT("OMBRE"), TEXT("SFUMATURA"), TEXT("SCHATTEN"),
		TEXT("SOMBRA"), TEXT("ТЕНЬ"), TEXT("GÖLGE"), TEXT("تظليل") } },
	// TrayRelease
	{ { TEXT("move toward a cup   ·   release to load it"), TEXT("カップの方へ動かす   ·   離すと装填"),
		TEXT("往那杯的方向推   ·   放開就沾上"), TEXT("往那杯的方向推   ·   松开就沾上"), TEXT("원하는 컵 쪽으로 움직인다   ·   놓으면 장착"),
		TEXT("muévete hacia un tintero   ·   suelta para cargarlo"), TEXT("déplace-toi vers un godet   ·   relâche pour charger"), TEXT("muoviti verso un contenitore   ·   rilascia per caricarlo"), TEXT("in Richtung eines Napfs bewegen   ·   loslassen zum Laden"),
		TEXT("mova na direção de um pote   ·   solte para carregar"), TEXT("двинь в сторону стаканчика   ·   отпусти, чтобы набрать"), TEXT("bir kaba doğru hareket et   ·   bırakınca yüklenir"), TEXT("حرّك باتجاه كوب   ·   أفلت للتحميل") } },
	// TraySwitchNeedle
	{ { TEXT("Q to switch"), TEXT("Q で切替"), TEXT("Q 換筆"), TEXT("Q 换笔"),
		TEXT("Q 로 전환"), TEXT("Q para cambiar"), TEXT("Q pour changer"), TEXT("Q per cambiare"), TEXT("Q zum Wechseln"),
		TEXT("Q para trocar"), TEXT("Q — сменить"), TEXT("değiştir: Q"), TEXT("اضغط Q للتبديل") } },
	// HudOutOfReach
	{ { TEXT("out of reach — lean closer (WASD to stand up)"), TEXT("届かない — もっと近づく（WASD で立つ）"), TEXT("搆不到 — 靠近一點（WASD 起身）"), TEXT("够不到 — 靠近一点（WASD 起身）"),
		TEXT("닿지 않는다 — 더 가까이 (WASD 로 일어서기)"), TEXT("fuera de alcance — acércate (WASD para levantarte)"), TEXT("hors de portée — approche-toi (WASD pour te lever)"), TEXT("fuori portata — avvicinati (WASD per alzarti)"), TEXT("außer Reichweite — näher ran (WASD zum Aufstehen)"),
		TEXT("fora de alcance — chegue mais perto (WASD para levantar)"), TEXT("не дотянуться — придвинься (WASD — встать)"), TEXT("erişemiyorsun — yaklaş (kalkmak için WASD)"), TEXT("بعيد عن المتناول — اقترب (WASD للوقوف)") } },
	// HudFlipAsk
	{ { TEXT("FLIP the body? — F to agree ({0}/{1})"), TEXT("体を裏返す？ — F で賛成（{0}/{1}）"), TEXT("把他翻過來？ — F 同意（{0}/{1}）"), TEXT("把他翻过来？ — F 同意（{0}/{1}）"),
		TEXT("몸을 뒤집을까? — F 로 찬성 ({0}/{1})"), TEXT("¿Darle la vuelta? — F para aceptar ({0}/{1})"), TEXT("Le retourner ? — F pour accepter ({0}/{1})"), TEXT("Girarlo? — F per accettare ({0}/{1})"), TEXT("Umdrehen? — F zum Zustimmen ({0}/{1})"),
		TEXT("Virar o corpo? — F para concordar ({0}/{1})"), TEXT("Перевернуть его? — F — согласиться ({0}/{1})"), TEXT("Çevirelim mi? — kabul için F ({0}/{1})"), TEXT("نقلبه؟ — اضغط F للموافقة ({0}/{1})") } },
	// HudFlipWait
	{ { TEXT("flipping — waiting for the others ({0}/{1})"), TEXT("裏返し — 他の人を待っている（{0}/{1}）"), TEXT("翻身中 — 等其他人（{0}/{1}）"), TEXT("翻身中 — 等其他人（{0}/{1}）"),
		TEXT("뒤집는 중 — 다른 사람 기다리는 중 ({0}/{1})"), TEXT("dándole la vuelta — esperando a los demás ({0}/{1})"), TEXT("retournement — en attente des autres ({0}/{1})"), TEXT("ribaltamento — in attesa degli altri ({0}/{1})"), TEXT("Umdrehen — warte auf die anderen ({0}/{1})"),
		TEXT("virando — esperando os outros ({0}/{1})"), TEXT("переворот — ждём остальных ({0}/{1})"), TEXT("çevriliyor — diğerleri bekleniyor ({0}/{1})"), TEXT("جارٍ القلب — بانتظار الآخرين ({0}/{1})") } },
	// HudShakeBought
	{ { TEXT("DREAM SHAKEN   -$500"), TEXT("夢を揺らした   -$500"), TEXT("夢被搖晃了   -$500"), TEXT("梦被摇晃了   -$500"),
		TEXT("꿈을 흔들었다   -$500"), TEXT("SUEÑO SACUDIDO   -$500"), TEXT("RÊVE SECOUÉ   -$500"), TEXT("SOGNO SCOSSO   -$500"), TEXT("TRAUM GESCHÜTTELT   -$500"),
		TEXT("SONHO SACUDIDO   -$500"), TEXT("СОН ВСТРЯХНУТ   -$500"), TEXT("RÜYA SARSILDI   -$500"), TEXT("اهتزّ الحلم   -$500") } },
	// HudShakeRefused
	{ { TEXT("SHAKE REFUSED (cash / cooldown)"), TEXT("揺らせない（所持金／クールダウン）"), TEXT("搖不了（現金／冷卻）"), TEXT("摇不了（现金／冷却）"),
		TEXT("흔들 수 없다 (자금／쿨다운)"), TEXT("NO SE PUDO (dinero / enfriamiento)"), TEXT("REFUSÉ (argent / recharge)"), TEXT("RIFIUTATO (soldi / attesa)"), TEXT("ABGELEHNT (Geld / Abklingzeit)"),
		TEXT("RECUSADO (dinheiro / recarga)"), TEXT("НЕ ВЫШЛО (деньги / откат)"), TEXT("OLMADI (para / bekleme)"), TEXT("مرفوض (المال / التبريد)") } },
	// HudStandingDrawing
	{ { TEXT("F — propose a flip   ·   G — shake his dream (-$500)"), TEXT("F — 裏返しを提案   ·   G — 夢を揺らす（-$500）"), TEXT("F — 提議翻身   ·   G — 搖他的夢（-$500）"), TEXT("F — 提议翻身   ·   G — 摇他的梦（-$500）"),
		TEXT("F — 뒤집기 제안   ·   G — 꿈 흔들기 (-$500)"), TEXT("F — proponer darle la vuelta   ·   G — sacudir su sueño (-$500)"), TEXT("F — proposer de le retourner   ·   G — secouer son rêve (-$500)"), TEXT("F — proponi di girarlo   ·   G — scuoti il suo sogno (-$500)"), TEXT("F — Umdrehen vorschlagen   ·   G — seinen Traum schütteln (-$500)"),
		TEXT("F — propor virar   ·   G — sacudir o sonho dele (-$500)"), TEXT("F — предложить переворот   ·   G — встряхнуть его сон (-$500)"), TEXT("F — çevirmeyi öner   ·   G — rüyasını sars (-$500)"), TEXT("F — اقترح قلبه   ·   G — هزّ حلمه (-$500)") } },
	// ActFlip
	{ { TEXT("propose a flip"), TEXT("裏返しを提案"), TEXT("提議翻身"), TEXT("提议翻身"),
		TEXT("뒤집기 제안"), TEXT("proponer voltear"), TEXT("proposer un retournement"), TEXT("proponi il giro"), TEXT("Umdrehen vorschlagen"),
		TEXT("propor virar"), TEXT("предложить переворот"), TEXT("çevirmeyi öner"), TEXT("اقتراح القلب") } },
	// ActInk
	{ { TEXT("ink"), TEXT("彫る"), TEXT("上墨"), TEXT("上墨"),
		TEXT("새기기"), TEXT("tatuar"), TEXT("encrer"), TEXT("inchiostrare"), TEXT("tätowieren"),
		TEXT("tatuar"), TEXT("колоть"), TEXT("mürekkep"), TEXT("وشم") } },
	// ActCups
	{ { TEXT("ink cups"), TEXT("インクカップ"), TEXT("墨杯"), TEXT("墨杯"),
		TEXT("잉크 컵"), TEXT("tinteros"), TEXT("godets"), TEXT("colori"), TEXT("Farbnäpfe"),
		TEXT("potes de tinta"), TEXT("стаканчики"), TEXT("kaplar"), TEXT("أكواب الحبر") } },
	// ActWash
	{ { TEXT("wash"), TEXT("濃さ"), TEXT("濃淡"), TEXT("浓淡"),
		TEXT("농도"), TEXT("dilución"), TEXT("dilution"), TEXT("diluizione"), TEXT("Verdünnung"),
		TEXT("diluição"), TEXT("разбавка"), TEXT("seyreltme"), TEXT("تخفيف") } },
	// ActNeedle
	{ { TEXT("needle"), TEXT("針"), TEXT("換針"), TEXT("换针"),
		TEXT("바늘"), TEXT("aguja"), TEXT("aiguille"), TEXT("ago"), TEXT("Nadel"),
		TEXT("agulha"), TEXT("игла"), TEXT("iğne"), TEXT("إبرة") } },
	// ActShake
	{ { TEXT("shake his dream"), TEXT("夢を揺らす"), TEXT("搖他的夢"), TEXT("摇他的梦"),
		TEXT("꿈 흔들기"), TEXT("sacudir su sueño"), TEXT("secouer son rêve"), TEXT("scuoti il suo sogno"), TEXT("seinen Traum schütteln"),
		TEXT("sacudir o sonho"), TEXT("встряхнуть сон"), TEXT("rüyasını sars"), TEXT("هزّ حلمه") } },
	// ActStand
	{ { TEXT("stand up"), TEXT("立つ"), TEXT("起身"), TEXT("起身"),
		TEXT("일어서기"), TEXT("levantarse"), TEXT("se lever"), TEXT("alzarsi"), TEXT("aufstehen"),
		TEXT("levantar"), TEXT("встать"), TEXT("kalk"), TEXT("وقوف") } },
	// ActLeanIn（09-03：句子→動詞。鍵位由鍵帽圖形表達，文字只講「做什麼」）
	{ { TEXT("lean in"), TEXT("身を寄せる"), TEXT("湊近"), TEXT("凑近"),
		TEXT("몸을 기울인다"), TEXT("acércate"), TEXT("approche-toi"), TEXT("avvicinati"), TEXT("heranbeugen"),
		TEXT("chegue perto"), TEXT("придвинуться"), TEXT("yaklaş"), TEXT("انحنِ") } },
	// HudFeignSleep
	{ { TEXT("feigning sleep — release SHIFT to open your eyes"), TEXT("寝たふり中 — SHIFT を離すと目を開ける"), TEXT("裝睡中 — 放開 SHIFT 睜眼"), TEXT("装睡中 — 松开 SHIFT 睁眼"),
		TEXT("자는 척 — SHIFT 를 놓으면 눈을 뜬다"), TEXT("haciéndote el dormido — suelta SHIFT para abrir los ojos"), TEXT("tu fais semblant de dormir — relâche MAJ pour ouvrir les yeux"), TEXT("fingi di dormire — rilascia MAIUSC per aprire gli occhi"), TEXT("du stellst dich schlafend — UMSCHALT loslassen zum Augenöffnen"),
		TEXT("fingindo dormir — solte SHIFT para abrir os olhos"), TEXT("притворяешься спящим — отпусти SHIFT, чтобы открыть глаза"), TEXT("uyuyormuş gibi yapıyorsun — gözlerini açmak için SHIFT bırak"), TEXT("تتظاهر بالنوم — أفلت SHIFT لتفتح عينيك") } },
	// HudEyesOpen
	{ { TEXT("eyes open — hold SHIFT to feign sleep   ·   WASD stands you up and ends the drawing"), TEXT("目を開けている — SHIFT 長押しで寝たふり   ·   WASD で起き上がり彫りを終える"), TEXT("睜著眼 — 按住 SHIFT 裝睡   ·   WASD 起身結束作畫"), TEXT("睁着眼 — 按住 SHIFT 装睡   ·   WASD 起身结束作画"),
		TEXT("눈을 뜬 상태 — SHIFT 를 누르면 자는 척   ·   WASD 로 일어나 작업을 끝낸다"), TEXT("ojos abiertos — mantén SHIFT para fingir   ·   WASD te levanta y termina el tatuaje"), TEXT("yeux ouverts — maintiens MAJ pour faire semblant   ·   WASD te lève et met fin au tatouage"), TEXT("occhi aperti — tieni MAIUSC per fingere   ·   WASD ti alza e chiude il tatuaggio"), TEXT("Augen offen — UMSCHALT halten zum Vortäuschen   ·   WASD steht auf und beendet das Tätowieren"),
		TEXT("olhos abertos — segure SHIFT para fingir   ·   WASD levanta você e encerra a tatuagem"), TEXT("глаза открыты — зажми SHIFT, чтобы притвориться   ·   WASD поднимет тебя и закончит работу"), TEXT("gözler açık — numara için SHIFT basılı tut   ·   WASD seni kaldırır ve işi bitirir"), TEXT("عيناك مفتوحتان — اضغط SHIFT للتظاهر   ·   WASD ينهضك وينهي الوشم") } },
	// PhaseLobby
	{ { TEXT("LOBBY"), TEXT("控えの間"), TEXT("等候室"), TEXT("等候室"),
		TEXT("대기실"), TEXT("VESTÍBULO"), TEXT("SALON"), TEXT("ATRIO"), TEXT("VORRAUM"),
		TEXT("SAGUÃO"), TEXT("ЛОББИ"), TEXT("LOBİ"), TEXT("الردهة") } },
	// PhaseBottleSpin
	{ { TEXT("BOTTLE SPIN"), TEXT("瓶回し"), TEXT("轉酒瓶"), TEXT("转酒瓶"),
		TEXT("병 돌리기"), TEXT("LA BOTELLA"), TEXT("LA BOUTEILLE"), TEXT("LA BOTTIGLIA"), TEXT("FLASCHENDREHEN"),
		TEXT("A GARRAFA"), TEXT("БУТЫЛОЧКА"), TEXT("ŞİŞE ÇEVİRME"), TEXT("دوران الزجاجة") } },
	// PhaseSeating
	{ { TEXT("SEATING"), TEXT("入座"), TEXT("入座"), TEXT("入座"),
		TEXT("착석"), TEXT("EL BRINDIS"), TEXT("LE TOAST"), TEXT("IL BRINDISI"), TEXT("DER TRUNK"),
		TEXT("O BRINDE"), TEXT("ТОСТ"), TEXT("KADEH"), TEXT("الجلوس") } },
	// PhaseTour
	{ { TEXT("GALLERY TOUR"), TEXT("品評会"), TEXT("傑作巡禮"), TEXT("杰作巡礼"),
		TEXT("감상회"), TEXT("LA GALERÍA"), TEXT("LA GALERIE"), TEXT("LA GALLERIA"), TEXT("DIE GALERIE"),
		TEXT("A GALERIA"), TEXT("ГАЛЕРЕЯ"), TEXT("GALERİ"), TEXT("المعرض") } },
	// PhaseAccusation
	{ { TEXT("ACCUSATION"), TEXT("名指し"), TEXT("指認"), TEXT("指认"),
		TEXT("지목"), TEXT("LA ACUSACIÓN"), TEXT("L’ACCUSATION"), TEXT("L’ACCUSA"), TEXT("DIE ANKLAGE"),
		TEXT("A ACUSAÇÃO"), TEXT("ОБВИНЕНИЕ"), TEXT("SUÇLAMA"), TEXT("الاتهام") } },
	// PhaseResolution
	{ { TEXT("RESOLUTION"), TEXT("判定"), TEXT("判決"), TEXT("判决"),
		TEXT("판정"), TEXT("EL VEREDICTO"), TEXT("LE VERDICT"), TEXT("IL VERDETTO"), TEXT("DAS URTEIL"),
		TEXT("O VEREDITO"), TEXT("ВЕРДИКТ"), TEXT("KARAR"), TEXT("الحكم") } },
	// PhaseFinale
	{ { TEXT("FINALE"), TEXT("三杯目"), TEXT("第三杯"), TEXT("第三杯"),
		TEXT("세 잔째"), TEXT("EL FINAL"), TEXT("LA FIN"), TEXT("IL FINALE"), TEXT("DAS FINALE"),
		TEXT("O FINAL"), TEXT("ФИНАЛ"), TEXT("FİNAL"), TEXT("الختام") } },
	// PhasePostGame
	{ { TEXT("PARLOR"), TEXT("彫り物部屋"), TEXT("刺青房"), TEXT("刺青房"),
		TEXT("문신방"), TEXT("EL SALÓN"), TEXT("LE SALON"), TEXT("IL SALOTTO"), TEXT("DER SALON"),
		TEXT("O SALÃO"), TEXT("САЛОН"), TEXT("SALON"), TEXT("الصالون") } },
	// PhaseDream
	{ { TEXT("DRUNK DREAM"), TEXT("酔いの夢"), TEXT("醉夢"), TEXT("醉梦"),
		TEXT("취몽"), TEXT("EL SUEÑO EBRIO"), TEXT("LE RÊVE IVRE"), TEXT("IL SOGNO EBBRO"), TEXT("DER RAUSCHTRAUM"),
		TEXT("O SONHO ÉBRIO"), TEXT("ХМЕЛЬНОЙ СОН"), TEXT("SARHOŞ RÜYA"), TEXT("حلم السكر") } },
	// ImpLobbyWait
	{ { TEXT("wait for the room to fill"), TEXT("人が揃うのを待とう"), TEXT("等大家坐滿"), TEXT("等大家坐满"),
		TEXT("사람이 모이길 기다리자"), TEXT("espera a que se llene la sala"), TEXT("attends que la salle se remplisse"), TEXT("aspetta che la sala si riempia"), TEXT("warte, bis der Raum voll ist"),
		TEXT("espere a sala encher"), TEXT("жди, пока все соберутся"), TEXT("oda dolsun diye bekle"), TEXT("انتظر امتلاء الغرفة") } },
	// ImpLobbyHost
	{ { TEXT("start when everyone is in"), TEXT("全員揃ったら始めよう"), TEXT("人到齊就開局"), TEXT("人到齐就开局"),
		TEXT("다 모이면 시작하자"), TEXT("empieza cuando estén todos"), TEXT("commence quand tout le monde est là"), TEXT("inizia quando ci sono tutti"), TEXT("starte, wenn alle da sind"),
		TEXT("comece quando todos entrarem"), TEXT("начинай, когда все в сборе"), TEXT("herkes gelince başlat"), TEXT("ابدأ عندما يكتمل الجميع") } },
	// ImpBottleSpin
	{ { TEXT("see who the bottle points at"), TEXT("瓶の口が向く先を見よう"), TEXT("看瓶口指向誰"), TEXT("看瓶口指向谁"),
		TEXT("병이 누구를 가리키는지 보자"), TEXT("mira a quién señala la botella"), TEXT("regarde qui la bouteille désigne"), TEXT("guarda chi indica la bottiglia"), TEXT("sieh, auf wen die Flasche zeigt"),
		TEXT("veja para quem a garrafa aponta"), TEXT("смотри, на кого укажет бутылка"), TEXT("şişe kimi gösteriyor bak"), TEXT("انظر إلى من تشير الزجاجة") } },
	// ImpSeating
	{ { TEXT("he is falling asleep"), TEXT("彼は眠りに落ちる"), TEXT("他要睡了"), TEXT("他要睡了"),
		TEXT("그가 잠들고 있다"), TEXT("se está quedando dormido"), TEXT("il s’endort"), TEXT("si sta addormentando"), TEXT("er schläft gerade ein"),
		TEXT("ele está adormecendo"), TEXT("он засыпает"), TEXT("uykuya dalıyor"), TEXT("إنه يغفو") } },
	// ImpDrawStand
	{ { TEXT("lean in to his body"), TEXT("体に身を寄せよう"), TEXT("湊近他的身體"), TEXT("凑近他的身体"),
		TEXT("몸에 다가가자"), TEXT("acércate a su cuerpo"), TEXT("approche-toi de son corps"), TEXT("avvicinati al suo corpo"), TEXT("beuge dich zu seinem Körper"),
		TEXT("chegue perto do corpo dele"), TEXT("придвинься к его телу"), TEXT("vücuduna yaklaş"), TEXT("اقترب من جسده") } },
	// ImpDrawLocked
	{ { TEXT("ink him"), TEXT("彫ろう"), TEXT("畫下去"), TEXT("画下去"),
		TEXT("새기자"), TEXT("tatúalo"), TEXT("encre-le"), TEXT("inchiostralo"), TEXT("tätowiere ihn"),
		TEXT("tatue-o"), TEXT("коли его"), TEXT("onu döv"), TEXT("ارسم عليه") } },
	// ImpDream
	{ { TEXT("trace the line to the end"), TEXT("線を最後までなぞろう"), TEXT("沿著線描到底"), TEXT("沿着线描到底"),
		TEXT("선을 끝까지 따라가자"), TEXT("sigue la línea hasta el final"), TEXT("suis la ligne jusqu’au bout"), TEXT("segui la linea fino in fondo"), TEXT("folge der Linie bis zum Ende"),
		TEXT("siga a linha até o fim"), TEXT("веди по линии до конца"), TEXT("çizgiyi sonuna kadar izle"), TEXT("تتبع الخط حتى النهاية") } },
	// ImpFeign
	{ { TEXT("you are feigning sleep"), TEXT("寝たふり中"), TEXT("裝睡中"), TEXT("装睡中"),
		TEXT("자는 척하는 중"), TEXT("estás fingiendo dormir"), TEXT("tu fais semblant de dormir"), TEXT("stai fingendo di dormire"), TEXT("du stellst dich schlafend"),
		TEXT("você está fingindo dormir"), TEXT("ты притворяешься спящим"), TEXT("uyuyor numarası yapıyorsun"), TEXT("أنت تتظاهر بالنوم") } },
	// ImpTour
	{ { TEXT("look at every piece"), TEXT("一枚ずつよく見よう"), TEXT("看清楚每一幅"), TEXT("看清楚每一幅"),
		TEXT("한 점씩 잘 보자"), TEXT("mira bien cada obra"), TEXT("regarde bien chaque œuvre"), TEXT("guarda bene ogni opera"), TEXT("sieh dir jedes Werk an"),
		TEXT("olhe bem cada obra"), TEXT("рассмотри каждую работу"), TEXT("her esere iyi bak"), TEXT("انظر جيدا إلى كل عمل") } },
	// ImpAccuseVictim
	{ { TEXT("name who drew this one"), TEXT("これを彫った奴を指せ"), TEXT("指出這幅是誰畫的"), TEXT("指出这幅是谁画的"),
		TEXT("이걸 새긴 자를 지목하라"), TEXT("di quién dibujó esta"), TEXT("désigne qui a dessiné celle-ci"), TEXT("indica chi ha disegnato questa"), TEXT("nenne, wer das gestochen hat"),
		TEXT("diga quem desenhou esta"), TEXT("назови, кто это нарисовал"), TEXT("bunu kimin çizdiğini söyle"), TEXT("سم من رسم هذه") } },
	// ImpAccuseOther
	{ { TEXT("do not let him recognise you"), TEXT("気づかれるな"), TEXT("別讓他認出你"), TEXT("别让他认出你"),
		TEXT("들키지 마라"), TEXT("que no te reconozca"), TEXT("ne te fais pas reconnaître"), TEXT("non farti riconoscere"), TEXT("lass dich nicht erkennen"),
		TEXT("não deixe ele te reconhecer"), TEXT("не дай себя узнать"), TEXT("seni tanımasına izin verme"), TEXT("لا تدعه يتعرف عليك") } },
	// ImpPostGame
	{ { TEXT("look over your ink"), TEXT("己の彫り物を眺めよう"), TEXT("端詳你的刺青"), TEXT("端详你的刺青"),
		TEXT("자신의 문신을 살펴보자"), TEXT("observa tus tatuajes"), TEXT("contemple tes tatouages"), TEXT("osserva i tuoi tatuaggi"), TEXT("betrachte deine Tätowierungen"),
		TEXT("observe suas tatuagens"), TEXT("рассмотри свои татуировки"), TEXT("dövmelerine bak"), TEXT("تأمل وشومك") } },
	// ActStartMatch
	{ { TEXT("start"), TEXT("開始"), TEXT("開局"), TEXT("开局"),
		TEXT("시작"), TEXT("empezar"), TEXT("démarrer"), TEXT("inizia"), TEXT("starten"),
		TEXT("começar"), TEXT("начать"), TEXT("başlat"), TEXT("ابدأ") } },
	// ActKick
	{ { TEXT("kick"), TEXT("追い出す"), TEXT("踢人"), TEXT("踢人"),
		TEXT("내보내기"), TEXT("expulsar"), TEXT("expulser"), TEXT("espelli"), TEXT("rauswerfen"),
		TEXT("expulsar"), TEXT("выгнать"), TEXT("at"), TEXT("اطرد") } },
	// ActTrace
	{ { TEXT("trace"), TEXT("なぞる"), TEXT("描線"), TEXT("描线"),
		TEXT("따라 그리기"), TEXT("trazar"), TEXT("tracer"), TEXT("traccia"), TEXT("nachziehen"),
		TEXT("traçar"), TEXT("вести"), TEXT("izle"), TEXT("تتبع") } },
	// ActWake
	{ { TEXT("open eyes"), TEXT("目を開ける"), TEXT("睜眼"), TEXT("睁眼"),
		TEXT("눈 뜨기"), TEXT("abrir ojos"), TEXT("ouvrir les yeux"), TEXT("apri gli occhi"), TEXT("Augen öffnen"),
		TEXT("abrir os olhos"), TEXT("открыть глаза"), TEXT("gözleri aç"), TEXT("افتح عينيك") } },
	// ActPickWork
	{ { TEXT("pick work"), TEXT("作品を選ぶ"), TEXT("選畫"), TEXT("选画"),
		TEXT("작품 고르기"), TEXT("elegir obra"), TEXT("choisir l’œuvre"), TEXT("scegli l’opera"), TEXT("Werk wählen"),
		TEXT("escolher obra"), TEXT("выбрать работу"), TEXT("eser seç"), TEXT("اختر العمل") } },
	// ActSuspect
	{ { TEXT("next suspect"), TEXT("次の容疑者"), TEXT("換嫌疑人"), TEXT("换嫌疑人"),
		TEXT("다음 용의자"), TEXT("siguiente sospechoso"), TEXT("suspect suivant"), TEXT("prossimo sospetto"), TEXT("nächster Verdächtiger"),
		TEXT("próximo suspeito"), TEXT("следующий подозреваемый"), TEXT("sonraki şüpheli"), TEXT("المشتبه التالي") } },
	// ActAccuse
	{ { TEXT("accuse"), TEXT("名指し"), TEXT("指認"), TEXT("指认"),
		TEXT("지목"), TEXT("acusar"), TEXT("accuser"), TEXT("accusa"), TEXT("anklagen"),
		TEXT("acusar"), TEXT("обвинить"), TEXT("suçla"), TEXT("اتهم") } },
	// ActLaser
	{ { TEXT("laser"), TEXT("レーザー"), TEXT("雷射"), TEXT("激光"),
		TEXT("레이저"), TEXT("láser"), TEXT("laser"), TEXT("laser"), TEXT("Laser"),
		TEXT("laser"), TEXT("лазер"), TEXT("lazer"), TEXT("ليزر") } },
	// ActNextMatch
	{ { TEXT("next match"), TEXT("次の一戦"), TEXT("下一場"), TEXT("下一场"),
		TEXT("다음 판"), TEXT("siguiente partida"), TEXT("partie suivante"), TEXT("prossima partita"), TEXT("nächste Runde"),
		TEXT("próxima partida"), TEXT("следующий матч"), TEXT("sonraki maç"), TEXT("الجولة التالية") } },
	// RuleDraw1
	{ { TEXT("he wakes when his dream is traced."), TEXT("夢をなぞり終えると彼は目を覚ます。"), TEXT("他描完夢就會醒。"), TEXT("他描完梦就会醒。"),
		TEXT("꿈을 다 따라 그리면 그는 깨어난다."), TEXT("despierta cuando termina de trazar su sueño."), TEXT("il se réveille quand son rêve est tracé."), TEXT("si sveglia quando il sogno è tracciato."), TEXT("er erwacht, wenn sein Traum nachgezogen ist."),
		TEXT("ele acorda quando o sonho é traçado."), TEXT("он проснётся, когда обведёт свой сон."), TEXT("rüyasını çizip bitirince uyanır."), TEXT("يستيقظ عندما يكمل تتبع حلمه.") } },
	// RuleDraw2
	{ { TEXT("works he cannot name become real tattoos."), TEXT("見抜けなかった絵は本物の刺青になる。"), TEXT("他認不出的畫會變成真刺青。"), TEXT("他认不出的画会变成真刺青。"),
		TEXT("그가 못 맞힌 그림은 진짜 문신이 된다."), TEXT("las obras que no acierte serán tatuajes reales."), TEXT("les œuvres qu’il n’identifie pas deviennent de vrais tatouages."), TEXT("le opere che non indovina diventano tatuaggi veri."), TEXT("Werke, die er nicht errät, werden echte Tattoos."),
		TEXT("as obras que ele não acertar viram tatuagens reais."), TEXT("работы, автора которых он не угадает, станут настоящими."), TEXT("bilemediği eserler gerçek dövme olur."), TEXT("الأعمال التي لا يعرف صاحبها تصبح وشما حقيقيا.") } },
	// RuleDream1
	{ { TEXT("trace to the end to wake up."), TEXT("最後までなぞれば目が覚める。"), TEXT("描到底就會醒來。"), TEXT("描到底就会醒来。"),
		TEXT("끝까지 따라 그리면 깨어난다."), TEXT("traza hasta el final para despertar."), TEXT("trace jusqu’au bout pour te réveiller."), TEXT("traccia fino in fondo per svegliarti."), TEXT("zieh bis zum Ende nach, um aufzuwachen."),
		TEXT("trace até o fim para acordar."), TEXT("доведи линию до конца, чтобы проснуться."), TEXT("uyanmak için sonuna kadar çiz."), TEXT("تتبع حتى النهاية لتستيقظ.") } },
	// RuleDream2
	{ { TEXT("leave the line and you start over."), TEXT("線を外れたらやり直し。"), TEXT("描出線就要重來。"), TEXT("描出线就要重来。"),
		TEXT("선을 벗어나면 처음부터 다시."), TEXT("si te sales de la línea, vuelves a empezar."), TEXT("si tu sors de la ligne, tu recommences."), TEXT("se esci dalla linea, ricominci."), TEXT("verlässt du die Linie, fängst du von vorn an."),
		TEXT("se sair da linha, recomeça."), TEXT("сойдёшь с линии — начнёшь заново."), TEXT("çizgiden çıkarsan baştan başlarsın."), TEXT("إذا خرجت عن الخط تبدأ من جديد.") } },
	// RuleAccuse1
	{ { TEXT("name the artist of each work."), TEXT("一枚ずつ彫った奴を当てる。"), TEXT("逐幅指認作者。"), TEXT("逐幅指认作者。"),
		TEXT("작품마다 새긴 자를 맞힌다."), TEXT("acierta el autor de cada obra."), TEXT("devine l’auteur de chaque œuvre."), TEXT("indovina l’autore di ogni opera."), TEXT("errate den Urheber jedes Werks."),
		TEXT("acerte o autor de cada obra."), TEXT("угадай автора каждой работы."), TEXT("her eserin sahibini bil."), TEXT("خمن صاحب كل عمل.") } },
	// RuleAccuse2
	{ { TEXT("guess wrong and it stays on you forever."), TEXT("外せばその絵は一生残る。"), TEXT("猜錯那幅就永遠留在身上。"), TEXT("猜错那幅就永远留在身上。"),
		TEXT("틀리면 그 그림은 평생 남는다."), TEXT("si fallas, se queda contigo para siempre."), TEXT("si tu te trompes, elle reste à jamais."), TEXT("se sbagli, resta per sempre."), TEXT("rätst du falsch, bleibt es für immer."),
		TEXT("se errar, fica para sempre."), TEXT("ошибёшься — останется навсегда."), TEXT("yanılırsan sonsuza dek kalır."), TEXT("إذا أخطأت تبقى إلى الأبد.") } },
	// BannerCorrect
	{ { TEXT("CORRECT!"), TEXT("的中！"), TEXT("猜對了！"), TEXT("猜对了！"),
		TEXT("정답!"), TEXT("¡ACIERTO!"), TEXT("TROUVÉ !"), TEXT("INDOVINATO!"), TEXT("RICHTIG!"),
		TEXT("ACERTOU!"), TEXT("УГАДАЛ!"), TEXT("BİLDİ!"), TEXT("إصابة!") } },
	// BannerWrong
	{ { TEXT("WRONG!"), TEXT("外れ！"), TEXT("猜錯了！"), TEXT("猜错了！"),
		TEXT("오답!"), TEXT("¡FALLO!"), TEXT("RATÉ !"), TEXT("SBAGLIATO!"), TEXT("FALSCH!"),
		TEXT("ERROU!"), TEXT("МИМО!"), TEXT("YANLIŞ!"), TEXT("خطأ!") } },
	// BannerOutCold
	{ { TEXT("OUT COLD"), TEXT("昏倒"), TEXT("昏死"), TEXT("昏死"),
		TEXT("기절"), TEXT("FUERA DE JUEGO"), TEXT("K.-O."), TEXT("STESO"), TEXT("AUSGEKNOCKT"),
		TEXT("APAGOU"), TEXT("В ОТКЛЮЧКЕ"), TEXT("KENDİNDEN GEÇTİ"), TEXT("فاقد الوعي") } },
	// ResCorrect
	{ { TEXT("takes the seat"), TEXT("席を明け渡す"), TEXT("換他上座"), TEXT("换他上座"),
		TEXT("자리를 넘긴다"), TEXT("cede el asiento"), TEXT("cède sa place"), TEXT("cede il posto"), TEXT("gibt den Platz ab"),
		TEXT("cede o lugar"), TEXT("уступает место"), TEXT("yerini devreder"), TEXT("يتنازل عن المقعد") } },
	// ResWrong
	{ { TEXT("inks the picked work   ·   +1 cup"), TEXT("選ばれた絵が本物に   ·   罰杯 +1"), TEXT("那幅變成真刺青   ·   罰酒 +1"), TEXT("那幅变成真刺青   ·   罚酒 +1"),
		TEXT("고른 그림이 진짜가 된다   ·   벌주 +1"), TEXT("la obra elegida se vuelve real   ·   +1 copa"), TEXT("l’œuvre choisie devient réelle   ·   +1 coupe"), TEXT("l’opera scelta diventa vera   ·   +1 coppa"), TEXT("das gewählte Werk wird echt   ·   +1 Becher"),
		TEXT("a obra escolhida vira real   ·   +1 taça"), TEXT("выбранная работа станет настоящей   ·   +1 чарка"), TEXT("seçilen eser gerçek olur   ·   +1 kadeh"), TEXT("العمل المختار يصبح حقيقيا   ·   كأس +1") } },
	// FinaleNote
	{ { TEXT("cash is split   ·   ink locked forever"), TEXT("現金は山分け   ·   刺青は一生もの"), TEXT("現金瓜分   ·   刺青永久固定"), TEXT("现金瓜分   ·   刺青永久固定"),
		TEXT("현금은 나눠 갖고   ·   문신은 평생"), TEXT("el dinero se reparte   ·   la tinta queda para siempre"), TEXT("l’argent est partagé   ·   l’encre reste à jamais"), TEXT("i soldi si dividono   ·   l’inchiostro resta per sempre"), TEXT("das Geld wird geteilt   ·   die Tinte bleibt für immer"),
		TEXT("o dinheiro é dividido   ·   a tinta fica para sempre"), TEXT("деньги делят   ·   тушь остаётся навсегда"), TEXT("para paylaşılır   ·   mürekkep sonsuza dek kalır"), TEXT("يقسم المال   ·   ويبقى الحبر للأبد") } },
	// LobbySeat
	{ { TEXT("seat {0}"), TEXT("席 {0}"), TEXT("第 {0} 席"), TEXT("第 {0} 席"),
		TEXT("{0}번 자리"), TEXT("asiento {0}"), TEXT("place {0}"), TEXT("posto {0}"), TEXT("Platz {0}"),
		TEXT("lugar {0}"), TEXT("место {0}"), TEXT("koltuk {0}"), TEXT("مقعد {0}") } },
	// LobbyHostTag
	{ { TEXT("host"), TEXT("部屋主"), TEXT("房主"), TEXT("房主"),
		TEXT("방장"), TEXT("anfitrión"), TEXT("hôte"), TEXT("host"), TEXT("Gastgeber"),
		TEXT("anfitrião"), TEXT("хост"), TEXT("kurucu"), TEXT("المضيف") } },
	// PostInk
	{ { TEXT("your ink:  {0} carbon  ·  {1} permanent"), TEXT("彫り物：カーボン {0}  ·  本彫り {1}"), TEXT("你的刺青：碳黑 {0}  ·  永久 {1}"), TEXT("你的刺青：碳黑 {0}  ·  永久 {1}"),
		TEXT("네 문신: 카본 {0}  ·  영구 {1}"), TEXT("tu tinta:  {0} carbón  ·  {1} permanente"), TEXT("ton encre :  {0} carbone  ·  {1} permanent"), TEXT("il tuo inchiostro:  {0} carbone  ·  {1} permanente"), TEXT("deine Tinte:  {0} Kohle  ·  {1} dauerhaft"),
		TEXT("sua tinta:  {0} carbono  ·  {1} permanente"), TEXT("твоя тушь:  {0} уголь  ·  {1} навсегда"), TEXT("mürekkebin:  {0} karbon  ·  {1} kalıcı"), TEXT("حبرك:  {0} كربون  ·  {1} دائم") } },
	// MenuTitle
	{ { TEXT("MENU"), TEXT("メニュー"), TEXT("選單"), TEXT("菜单"),
		TEXT("메뉴"), TEXT("MENÚ"), TEXT("MENU"), TEXT("MENU"), TEXT("MENÜ"),
		TEXT("MENU"), TEXT("МЕНЮ"), TEXT("MENÜ"), TEXT("القائمة") } },
	// MenuResume
	{ { TEXT("resume"), TEXT("戻る"), TEXT("繼續"), TEXT("继续"),
		TEXT("계속하기"), TEXT("continuar"), TEXT("reprendre"), TEXT("riprendi"), TEXT("fortsetzen"),
		TEXT("continuar"), TEXT("продолжить"), TEXT("devam et"), TEXT("استئناف") } },
	// MenuLeave
	{ { TEXT("leave the room"), TEXT("部屋を出る"), TEXT("離開房間"), TEXT("离开房间"),
		TEXT("방 나가기"), TEXT("salir de la sala"), TEXT("quitter la salle"), TEXT("esci dalla sala"), TEXT("Raum verlassen"),
		TEXT("sair da sala"), TEXT("выйти из комнаты"), TEXT("odadan çık"), TEXT("غادر الغرفة") } },
	// MenuPlayers
	{ { TEXT("players"), TEXT("参加者"), TEXT("玩家"), TEXT("玩家"),
		TEXT("참가자"), TEXT("jugadores"), TEXT("joueurs"), TEXT("giocatori"), TEXT("Spieler"),
		TEXT("jogadores"), TEXT("игроки"), TEXT("oyuncular"), TEXT("اللاعبون") } },
	// MenuKick
	{ { TEXT("kick"), TEXT("追い出す"), TEXT("踢出"), TEXT("踢出"),
		TEXT("내보내기"), TEXT("expulsar"), TEXT("expulser"), TEXT("espelli"), TEXT("rauswerfen"),
		TEXT("expulsar"), TEXT("выгнать"), TEXT("at"), TEXT("اطرد") } },
	// MenuSensitivity
	{ { TEXT("mouse sensitivity"), TEXT("マウス感度"), TEXT("滑鼠靈敏度"), TEXT("鼠标灵敏度"),
		TEXT("마우스 감도"), TEXT("sensibilidad del ratón"), TEXT("sensibilité de la souris"), TEXT("sensibilità del mouse"), TEXT("Mausempfindlichkeit"),
		TEXT("sensibilidade do mouse"), TEXT("чувствительность мыши"), TEXT("fare hassasiyeti"), TEXT("حساسية الفأرة") } },
	// MenuVolume
	{ { TEXT("master volume"), TEXT("全体音量"), TEXT("總音量"), TEXT("总音量"),
		TEXT("전체 음량"), TEXT("volumen general"), TEXT("volume général"), TEXT("volume generale"), TEXT("Gesamtlautstärke"),
		TEXT("volume geral"), TEXT("общая громкость"), TEXT("ana ses"), TEXT("مستوى الصوت") } },
	// TrapTitle
	{ { TEXT("HE STEPPED ON YOU"), TEXT("踏まれた"), TEXT("他踩到你了"), TEXT("他踩到你了"),
		TEXT("밟혔다"), TEXT("TE HA PISADO"), TEXT("IL T’A MARCHÉ DESSUS"), TEXT("TI HA CALPESTATO"), TEXT("ER IST AUF DICH GETRETEN"),
		TEXT("ELE PISOU EM VOCÊ"), TEXT("ОН НАСТУПИЛ НА ТЕБЯ"), TEXT("ÜSTÜNE BASTI"), TEXT("لقد داس عليك") } },
	// TrapSpin
	{ { TEXT("SPIN HIS DREAM"), TEXT("夢を回せ"), TEXT("攪亂他的夢"), TEXT("搅乱他的梦"),
		TEXT("그의 꿈을 흔들어라"), TEXT("REVUELVE SU SUEÑO"), TEXT("BROUILLE SON RÊVE"), TEXT("STRAVOLGI IL SUO SOGNO"), TEXT("VERDREH SEINEN TRAUM"),
		TEXT("EMBARALHE O SONHO DELE"), TEXT("ЗАКРУТИ ЕГО СОН"), TEXT("RÜYASINI KARIŞTIR"), TEXT("اعبث بحلمه") } },
	// TrapHint
	{ { TEXT("scroll wheel   ·   locks in {0}s"), TEXT("ホイールを回せ   ·   あと {0} 秒"), TEXT("滾動滾輪   ·   {0} 秒後定案"), TEXT("滚动滚轮   ·   {0} 秒后定案"),
		TEXT("휠을 굴려라   ·   {0}초 후 확정"), TEXT("rueda del ratón   ·   se fija en {0}s"), TEXT("molette   ·   verrouillé dans {0}s"), TEXT("rotella   ·   si blocca tra {0}s"), TEXT("Mausrad   ·   fixiert in {0}s"),
		TEXT("roda do mouse   ·   trava em {0}s"), TEXT("колесо мыши   ·   зафиксируется через {0}с"), TEXT("tekerlek   ·   {0}sn sonra kilitlenir"), TEXT("عجلة الفأرة   ·   تثبت خلال {0} ثانية") } },
	// DreamShake
	{ { TEXT("{0} SHAKES YOUR DREAM !"), TEXT("{0} が夢を揺らす！"), TEXT("{0} 在搖你的夢！"), TEXT("{0} 在摇你的梦！"),
		TEXT("{0} 이(가) 꿈을 흔든다!"), TEXT("¡{0} SACUDE TU SUEÑO!"), TEXT("{0} SECOUE TON RÊVE !"), TEXT("{0} SCUOTE IL TUO SOGNO!"), TEXT("{0} SCHÜTTELT DEINEN TRAUM!"),
		TEXT("{0} SACODE SEU SONHO!"), TEXT("{0} ТРЯСЁТ ТВОЙ СОН!"), TEXT("{0} RÜYANI SARSIYOR!"), TEXT("{0} يهز حلمك!") } },
	// DreamSlipped
	{ { TEXT("SLIPPED — BACK TO THE START"), TEXT("はみ出した — 最初から"), TEXT("描出線 — 從頭來過"), TEXT("描出线 — 从头来过"),
		TEXT("선을 벗어났다 — 처음부터"), TEXT("TE SALISTE — VUELTA AL INICIO"), TEXT("TU AS DÉRAPÉ — ON RECOMMENCE"), TEXT("SEI USCITO — SI RICOMINCIA"), TEXT("ABGERUTSCHT — VON VORN"),
		TEXT("SAIU DA LINHA — DO COMEÇO"), TEXT("СОРВАЛСЯ — СНАЧАЛА"), TEXT("ÇİZGİDEN ÇIKTIN — BAŞTAN"), TEXT("انحرفت — من البداية") } },
	// DreamTrapped
	{ { TEXT("TRAPPED BY {0} !"), TEXT("{0} の罠だ！"), TEXT("中了 {0} 的陷阱！"), TEXT("中了 {0} 的陷阱！"),
		TEXT("{0} 의 함정이다!"), TEXT("¡TRAMPA DE {0}!"), TEXT("PIÈGE DE {0} !"), TEXT("TRAPPOLA DI {0}!"), TEXT("FALLE VON {0}!"),
		TEXT("ARMADILHA DE {0}!"), TEXT("ЛОВУШКА {0}!"), TEXT("{0} SENİ TUZAĞA DÜŞÜRDÜ!"), TEXT("فخ من {0}!") } },
	// DreamReels
	{ { TEXT("the dream reels..."), TEXT("夢が揺れる…"), TEXT("夢在晃…"), TEXT("梦在晃…"),
		TEXT("꿈이 흔들린다…"), TEXT("el sueño se tambalea..."), TEXT("le rêve vacille..."), TEXT("il sogno vacilla..."), TEXT("der Traum taumelt …"),
		TEXT("o sonho balança..."), TEXT("сон качается…"), TEXT("rüya sallanıyor..."), TEXT("الحلم يترنح...") } },
	// DreamCash
	{ { TEXT("the room helps itself to your cash..."), TEXT("身ぐるみ剥がされていく…"), TEXT("大家正在分你的錢…"), TEXT("大家正在分你的钱…"),
		TEXT("네 돈이 나눠지고 있다…"), TEXT("se reparten tu dinero..."), TEXT("on se partage ton argent..."), TEXT("si stanno dividendo i tuoi soldi..."), TEXT("dein Geld wird verteilt …"),
		TEXT("estão dividindo seu dinheiro..."), TEXT("твои деньги делят…"), TEXT("paranı paylaşıyorlar..."), TEXT("يقتسمون مالك...") } },
	// PoseFaceUp
	{ { TEXT("POSE — FACE UP"), TEXT("仰向け"), TEXT("姿勢 — 仰躺"), TEXT("姿势 — 仰躺"),
		TEXT("자세 — 위를 보고"), TEXT("POSTURA — BOCA ARRIBA"), TEXT("POSE — SUR LE DOS"), TEXT("POSA — SUPINO"), TEXT("HALTUNG — RÜCKENLAGE"),
		TEXT("POSE — DE COSTAS"), TEXT("ПОЗА — НА СПИНЕ"), TEXT("DURUŞ — SIRT ÜSTÜ"), TEXT("الوضع — على الظهر") } },
	// PoseFaceDown
	{ { TEXT("POSE — FACE DOWN"), TEXT("うつ伏せ"), TEXT("姿勢 — 趴臥"), TEXT("姿势 — 趴卧"),
		TEXT("자세 — 엎드려"), TEXT("POSTURA — BOCA ABAJO"), TEXT("POSE — SUR LE VENTRE"), TEXT("POSA — PRONO"), TEXT("HALTUNG — BAUCHLAGE"),
		TEXT("POSE — DE BRUÇOS"), TEXT("ПОЗА — НА ЖИВОТЕ"), TEXT("DURUŞ — YÜZ ÜSTÜ"), TEXT("الوضع — على البطن") } },
	// DreamProgress
	{ { TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"),
		TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"),
		TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%"), TEXT("{0}%") } },
	// AccuseWork
	{ { TEXT("work {0} / {1}"), TEXT("作品 {0} / {1}"), TEXT("第 {0} / {1} 幅"), TEXT("第 {0} / {1} 幅"),
		TEXT("작품 {0} / {1}"), TEXT("obra {0} / {1}"), TEXT("œuvre {0} / {1}"), TEXT("opera {0} / {1}"), TEXT("Werk {0} / {1}"),
		TEXT("obra {0} / {1}"), TEXT("работа {0} / {1}"), TEXT("eser {0} / {1}"), TEXT("العمل {0} / {1}") } },
	};

	// 表的大小寫在兩個地方必有一邊會舊：改成推導長度＋編譯期對賬。
	// 舊寫法 GTable[COUNT] 在少一列時**照樣編得過**（缺的列補 nullptr），
	// 然後在執行期給出空字串——無聲失敗。
	static_assert(UE_ARRAY_COUNT(GTable) == static_cast<int32>(ENiLocKey::COUNT),
		"NiLoc: ENiLocKey 與字串表列數不一致——每加一個鍵就要加一列 13 語");

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

FString TFmt(const UObject* Ctx, ENiLocKey Key, const FString& Arg0, const FString& Arg1)
{
	return T(Ctx, Key).Replace(TEXT("{0}"), *Arg0).Replace(TEXT("{1}"), *Arg1);
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
