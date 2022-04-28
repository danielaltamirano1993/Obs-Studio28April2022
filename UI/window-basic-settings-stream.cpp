#include <QMessageBox>
#include <QUrl>

#include "window-basic-settings.hpp"
#include "obs-frontend-api.h"
#include "obs-app.hpp"
#include "window-basic-main.hpp"
#include "qt-wrappers.hpp"
#include "url-push-button.hpp"

#ifdef BROWSER_AVAILABLE
#include <browser-panel.hpp>
#endif

#include "auth-oauth.hpp"

#include "ui-config.h"

#if YOUTUBE_ENABLED
#include "youtube-api-wrappers.hpp"
#endif

using namespace json11;

struct QCef;
struct QCefCookieManager;

extern QCef *cef;
extern QCefCookieManager *panel_cookies;

enum class Section : int {
	Connect,
	StreamKey,
};

void OBSBasicSettings::InitStreamPage()
{
	ui->connectAccount2->setVisible(false);
	ui->disconnectAccount->setVisible(false);
	ui->bandwidthTestEnable->setVisible(false);

	ui->twitchAddonDropdown->setVisible(false);
	ui->twitchAddonLabel->setVisible(false);

	ui->connectedAccountLabel->setVisible(false);
	ui->connectedAccountText->setVisible(false);

	int vertSpacing = ui->topStreamLayout->verticalSpacing();

	QMargins m = ui->topStreamLayout->contentsMargins();
	m.setBottom(vertSpacing / 2);
	ui->topStreamLayout->setContentsMargins(m);

	m = ui->loginPageLayout->contentsMargins();
	m.setTop(vertSpacing / 2);
	ui->loginPageLayout->setContentsMargins(m);

	m = ui->streamkeyPageLayout->contentsMargins();
	m.setTop(vertSpacing / 2);
	ui->streamkeyPageLayout->setContentsMargins(m);

	streamUi.Setup(ui->streamKeyLabel, ui->service, ui->server,
		       ui->customServer, ui->moreInfoButton,
		       ui->getStreamKeyButton);

	streamUi.LoadServices(false);

	ui->twitchAddonDropdown->addItem(
		QTStr("Basic.Settings.Stream.TTVAddon.None"));
	ui->twitchAddonDropdown->addItem(
		QTStr("Basic.Settings.Stream.TTVAddon.BTTV"));
	ui->twitchAddonDropdown->addItem(
		QTStr("Basic.Settings.Stream.TTVAddon.FFZ"));
	ui->twitchAddonDropdown->addItem(
		QTStr("Basic.Settings.Stream.TTVAddon.Both"));

	connect(ui->service, SIGNAL(currentIndexChanged(int)), &streamUi,
		SLOT(UpdateServerList()));
	connect(ui->service, SIGNAL(currentIndexChanged(int)), &streamUi,
		SLOT(UpdateKeyLink()));
	connect(ui->service, SIGNAL(currentIndexChanged(int)), this,
		SLOT(UpdateVodTrackSetting()));
	connect(ui->service, SIGNAL(currentIndexChanged(int)), this,
		SLOT(UpdateServiceRecommendations()));
	connect(ui->service, SIGNAL(currentIndexChanged(int)), this,
		SLOT(UpdateResFPSLimits()));
	connect(ui->customServer, SIGNAL(textChanged(const QString &)),
		&streamUi, SLOT(UpdateKeyLink()));
	connect(ui->ignoreRecommended, SIGNAL(clicked(bool)), this,
		SLOT(DisplayEnforceWarning(bool)));
	connect(ui->ignoreRecommended, SIGNAL(toggled(bool)), this,
		SLOT(UpdateResFPSLimits()));
	connect(ui->service, SIGNAL(currentIndexChanged(int)), &streamUi,
		SLOT(UpdateMoreInfoLink()));
}

void OBSBasicSettings::LoadStream1Settings()
{
	bool ignoreRecommended =
		config_get_bool(main->Config(), "Stream1", "IgnoreRecommended");

	obs_service_t *service_obj = main->GetService();
	const char *type = obs_service_get_type(service_obj);

	loading = true;

	OBSDataAutoRelease settings = obs_service_get_settings(service_obj);

	const char *service = obs_data_get_string(settings, "service");
	const char *server = obs_data_get_string(settings, "server");
	const char *key = obs_data_get_string(settings, "key");

	if (strcmp(type, "rtmp_custom") == 0) {
		ui->service->setCurrentIndex(0);
		ui->customServer->setText(server);

		bool use_auth = obs_data_get_bool(settings, "use_auth");
		const char *username =
			obs_data_get_string(settings, "username");
		const char *password =
			obs_data_get_string(settings, "password");
		ui->authUsername->setText(QT_UTF8(username));
		ui->authPw->setText(QT_UTF8(password));
		ui->useAuth->setChecked(use_auth);
	} else {
		int idx = ui->service->findText(service);
		if (idx == -1) {
			if (service && *service)
				ui->service->insertItem(1, service);
			idx = 1;
		}
		ui->service->setCurrentIndex(idx);

		bool bw_test = obs_data_get_bool(settings, "bwtest");
		ui->bandwidthTestEnable->setChecked(bw_test);

		idx = config_get_int(main->Config(), "Twitch", "AddonChoice");
		ui->twitchAddonDropdown->setCurrentIndex(idx);
	}

	streamUi.UpdateServerList();

	if (strcmp(type, "rtmp_common") == 0) {
		int idx = ui->server->findData(server);
		if (idx == -1) {
			if (server && *server)
				ui->server->insertItem(0, server, server);
			idx = 0;
		}
		ui->server->setCurrentIndex(idx);
	}

	ui->key->setText(key);

	streamUi.ClearLastService();
	on_service_currentIndexChanged(0);

	streamUi.UpdateKeyLink();
	streamUi.UpdateMoreInfoLink();
	UpdateVodTrackSetting();
	UpdateServiceRecommendations();

	bool streamActive = obs_frontend_streaming_active();
	ui->streamPage->setEnabled(!streamActive);

	ui->ignoreRecommended->setChecked(ignoreRecommended);

	loading = false;

	QMetaObject::invokeMethod(this, "UpdateResFPSLimits",
				  Qt::QueuedConnection);
}

void OBSBasicSettings::SaveStream1Settings()
{
	bool customServer = streamUi.IsCustomService();
	const char *service_id = customServer ? "rtmp_custom" : "rtmp_common";

	obs_service_t *oldService = main->GetService();
	OBSDataAutoRelease hotkeyData = obs_hotkeys_save_service(oldService);

	OBSDataAutoRelease settings = obs_data_create();

	if (!customServer) {
		obs_data_set_string(settings, "service",
				    QT_TO_UTF8(ui->service->currentText()));
		obs_data_set_string(
			settings, "server",
			QT_TO_UTF8(ui->server->currentData().toString()));
	} else {
		obs_data_set_string(
			settings, "server",
			QT_TO_UTF8(ui->customServer->text().trimmed()));
		obs_data_set_bool(settings, "use_auth",
				  ui->useAuth->isChecked());
		if (ui->useAuth->isChecked()) {
			obs_data_set_string(
				settings, "username",
				QT_TO_UTF8(ui->authUsername->text()));
			obs_data_set_string(settings, "password",
					    QT_TO_UTF8(ui->authPw->text()));
		}
	}

	if (!!auth && strcmp(auth->service(), "Twitch") == 0) {
		bool choiceExists = config_has_user_value(
			main->Config(), "Twitch", "AddonChoice");
		int currentChoice =
			config_get_int(main->Config(), "Twitch", "AddonChoice");
		int newChoice = ui->twitchAddonDropdown->currentIndex();

		config_set_int(main->Config(), "Twitch", "AddonChoice",
			       newChoice);

		if (choiceExists && currentChoice != newChoice)
			forceAuthReload = true;

		obs_data_set_bool(settings, "bwtest",
				  ui->bandwidthTestEnable->isChecked());
	} else {
		obs_data_set_bool(settings, "bwtest", false);
	}

	obs_data_set_string(settings, "key", QT_TO_UTF8(ui->key->text()));

	OBSServiceAutoRelease newService = obs_service_create(
		service_id, "default_service", settings, hotkeyData);

	if (!newService)
		return;

	main->SetService(newService);
	main->SaveService();
	main->auth = auth;
	if (!!main->auth) {
		main->auth->LoadUI();
		main->SetBroadcastFlowEnabled(main->auth->broadcastFlow());
	} else {
		main->SetBroadcastFlowEnabled(false);
	}

	SaveCheckBox(ui->ignoreRecommended, "Stream1", "IgnoreRecommended");
}

static inline bool is_auth_service(const std::string &service)
{
	return Auth::AuthType(service) != Auth::Type::None;
}

static inline bool is_external_oauth(const std::string &service)
{
	return Auth::External(service);
}

static void reset_service_ui_fields(Ui::OBSBasicSettings *ui,
				    std::string &service, bool loading)
{
	bool external_oauth = is_external_oauth(service);
	if (external_oauth) {
		ui->streamKeyWidget->setVisible(false);
		ui->streamKeyLabel->setVisible(false);
		ui->connectAccount2->setVisible(true);
		ui->useStreamKeyAdv->setVisible(true);
		ui->streamStackWidget->setCurrentIndex((int)Section::StreamKey);
	} else if (cef) {
		QString key = ui->key->text();
		bool can_auth = is_auth_service(service);
		int page = can_auth && (!loading || key.isEmpty())
				   ? (int)Section::Connect
				   : (int)Section::StreamKey;

		ui->streamStackWidget->setCurrentIndex(page);
		ui->streamKeyWidget->setVisible(true);
		ui->streamKeyLabel->setVisible(true);
		ui->connectAccount2->setVisible(can_auth);
		ui->useStreamKeyAdv->setVisible(false);
	} else {
		ui->connectAccount2->setVisible(false);
		ui->useStreamKeyAdv->setVisible(false);
	}

	ui->connectedAccountLabel->setVisible(false);
	ui->connectedAccountText->setVisible(false);
	ui->disconnectAccount->setVisible(false);
}

#if YOUTUBE_ENABLED
static void get_yt_ch_title(Ui::OBSBasicSettings *ui)
{
	const char *name = config_get_string(OBSBasic::Get()->Config(),
					     "YouTube", "ChannelName");
	if (name) {
		ui->connectedAccountText->setText(name);
	} else {
		// if we still not changed the service page
		if (IsYouTubeService(QT_TO_UTF8(ui->service->currentText()))) {
			ui->connectedAccountText->setText(
				QTStr("Auth.LoadingChannel.Error"));
		}
	}
}
#endif

void OBSBasicSettings::UseStreamKeyAdvClicked()
{
	ui->streamKeyWidget->setVisible(true);
}

void OBSBasicSettings::on_service_currentIndexChanged(int)
{
	bool showMore = ui->service->currentData().toInt() ==
			(int)ListOpt::ShowAll;
	if (showMore)
		return;

	std::string service = QT_TO_UTF8(ui->service->currentText());
	bool custom = streamUi.IsCustomService();

	ui->disconnectAccount->setVisible(false);
	ui->bandwidthTestEnable->setVisible(false);
	ui->twitchAddonDropdown->setVisible(false);
	ui->twitchAddonLabel->setVisible(false);

	if (streamUi.LastService() != service.c_str()) {
		reset_service_ui_fields(ui.get(), service, loading);
	}

	ui->useAuth->setVisible(custom);
	ui->authUsernameLabel->setVisible(custom);
	ui->authUsername->setVisible(custom);
	ui->authPwLabel->setVisible(custom);
	ui->authPwWidget->setVisible(custom);

	if (custom) {
		ui->streamkeyPageLayout->insertRow(1, ui->serverLabel,
						   ui->serverStackedWidget);

		ui->serverStackedWidget->setCurrentIndex(1);
		ui->serverStackedWidget->setVisible(true);
		ui->serverLabel->setVisible(true);
		on_useAuth_toggled();
	} else {
		ui->serverStackedWidget->setCurrentIndex(0);
	}

	auth.reset();

	if (!main->auth) {
		return;
	}

	auto system_auth_service = main->auth->service();
	bool service_check = service.find(system_auth_service) !=
			     std::string::npos;
#if YOUTUBE_ENABLED
	service_check = service_check ? service_check
				      : IsYouTubeService(system_auth_service) &&
						IsYouTubeService(service);
#endif
	if (service_check) {
		auth = main->auth;
		OnAuthConnected();
	}
}

void OBSBasicSettings::on_show_clicked()
{
	if (ui->key->echoMode() == QLineEdit::Password) {
		ui->key->setEchoMode(QLineEdit::Normal);
		ui->show->setText(QTStr("Hide"));
	} else {
		ui->key->setEchoMode(QLineEdit::Password);
		ui->show->setText(QTStr("Show"));
	}
}

void OBSBasicSettings::on_authPwShow_clicked()
{
	if (ui->authPw->echoMode() == QLineEdit::Password) {
		ui->authPw->setEchoMode(QLineEdit::Normal);
		ui->authPwShow->setText(QTStr("Hide"));
	} else {
		ui->authPw->setEchoMode(QLineEdit::Password);
		ui->authPwShow->setText(QTStr("Show"));
	}
}

OBSService OBSBasicSettings::SpawnTempService()
{
	bool custom = streamUi.IsCustomService();
	const char *service_id = custom ? "rtmp_custom" : "rtmp_common";

	OBSDataAutoRelease settings = obs_data_create();

	if (!custom) {
		obs_data_set_string(settings, "service",
				    QT_TO_UTF8(ui->service->currentText()));
		obs_data_set_string(
			settings, "server",
			QT_TO_UTF8(ui->server->currentData().toString()));
	} else {
		obs_data_set_string(
			settings, "server",
			QT_TO_UTF8(ui->customServer->text().trimmed()));
	}
	obs_data_set_string(settings, "key", QT_TO_UTF8(ui->key->text()));

	OBSServiceAutoRelease newService = obs_service_create(
		service_id, "temp_service", settings, nullptr);
	return newService.Get();
}

void OBSBasicSettings::OnOAuthStreamKeyConnected()
{
	OAuthStreamKey *a = reinterpret_cast<OAuthStreamKey *>(auth.get());

	if (a) {
		bool validKey = !a->key().empty();

		if (validKey)
			ui->key->setText(QT_UTF8(a->key().c_str()));

		ui->streamKeyWidget->setVisible(false);
		ui->streamKeyLabel->setVisible(false);
		ui->connectAccount2->setVisible(false);
		ui->disconnectAccount->setVisible(true);
		ui->useStreamKeyAdv->setVisible(false);

		ui->connectedAccountLabel->setVisible(false);
		ui->connectedAccountText->setVisible(false);

		if (strcmp(a->service(), "Twitch") == 0) {
			ui->bandwidthTestEnable->setVisible(true);
			ui->twitchAddonLabel->setVisible(true);
			ui->twitchAddonDropdown->setVisible(true);
		} else {
			ui->bandwidthTestEnable->setChecked(false);
		}
#if YOUTUBE_ENABLED
		if (IsYouTubeService(a->service())) {
			ui->key->clear();

			ui->connectedAccountLabel->setVisible(true);
			ui->connectedAccountText->setVisible(true);

			ui->connectedAccountText->setText(
				QTStr("Auth.LoadingChannel.Title"));

			get_yt_ch_title(ui.get());
		}
#endif
	}

	ui->streamStackWidget->setCurrentIndex((int)Section::StreamKey);
}

void OBSBasicSettings::OnAuthConnected()
{
	std::string service = QT_TO_UTF8(ui->service->currentText());
	Auth::Type type = Auth::AuthType(service);

	if (type == Auth::Type::OAuth_StreamKey ||
	    type == Auth::Type::OAuth_LinkedAccount) {
		OnOAuthStreamKeyConnected();
	}

	if (!loading) {
		stream1Changed = true;
		EnableApplyButton(true);
	}
}

void OBSBasicSettings::on_connectAccount_clicked()
{
	std::string service = QT_TO_UTF8(ui->service->currentText());

	OAuth::DeleteCookies(service);

	auth = OAuthStreamKey::Login(this, service);
	if (!!auth) {
		OnAuthConnected();

		ui->useStreamKeyAdv->setVisible(false);
	}
}

#define DISCONNECT_COMFIRM_TITLE \
	"Basic.AutoConfig.StreamPage.DisconnectAccount.Confirm.Title"
#define DISCONNECT_COMFIRM_TEXT \
	"Basic.AutoConfig.StreamPage.DisconnectAccount.Confirm.Text"

void OBSBasicSettings::on_disconnectAccount_clicked()
{
	QMessageBox::StandardButton button;

	button = OBSMessageBox::question(this, QTStr(DISCONNECT_COMFIRM_TITLE),
					 QTStr(DISCONNECT_COMFIRM_TEXT));

	if (button == QMessageBox::No) {
		return;
	}

	main->auth.reset();
	auth.reset();
	main->SetBroadcastFlowEnabled(false);

	std::string service = QT_TO_UTF8(ui->service->currentText());

#ifdef BROWSER_AVAILABLE
	OAuth::DeleteCookies(service);
#endif

	ui->bandwidthTestEnable->setChecked(false);

	reset_service_ui_fields(ui.get(), service, loading);

	ui->bandwidthTestEnable->setVisible(false);
	ui->twitchAddonDropdown->setVisible(false);
	ui->twitchAddonLabel->setVisible(false);
	ui->key->setText("");

	ui->connectedAccountLabel->setVisible(false);
	ui->connectedAccountText->setVisible(false);
}

void OBSBasicSettings::on_useStreamKey_clicked()
{
	ui->streamStackWidget->setCurrentIndex((int)Section::StreamKey);
}

void OBSBasicSettings::on_useAuth_toggled()
{
	if (!streamUi.IsCustomService())
		return;

	bool use_auth = ui->useAuth->isChecked();

	ui->authUsernameLabel->setVisible(use_auth);
	ui->authUsername->setVisible(use_auth);
	ui->authPwLabel->setVisible(use_auth);
	ui->authPwWidget->setVisible(use_auth);
}

void OBSBasicSettings::UpdateVodTrackSetting()
{
	bool enableForCustomServer = config_get_bool(
		GetGlobalConfig(), "General", "EnableCustomServerVodTrack");
	bool enableVodTrack = ui->service->currentText() == "Twitch";
	bool wasEnabled = !!vodTrackCheckbox;

	if (enableForCustomServer && streamUi.IsCustomService())
		enableVodTrack = true;

	if (enableVodTrack == wasEnabled)
		return;

	if (!enableVodTrack) {
		delete vodTrackCheckbox;
		delete vodTrackContainer;
		delete simpleVodTrack;
		return;
	}

	/* -------------------------------------- */
	/* simple output mode vod track widgets   */

	bool simpleAdv = ui->simpleOutAdvanced->isChecked();
	bool vodTrackEnabled = config_get_bool(main->Config(), "SimpleOutput",
					       "VodTrackEnabled");

	simpleVodTrack = new QCheckBox(this);
	simpleVodTrack->setText(
		QTStr("Basic.Settings.Output.Simple.TwitchVodTrack"));
	simpleVodTrack->setVisible(simpleAdv);
	simpleVodTrack->setChecked(vodTrackEnabled);

	int pos;
	ui->simpleStreamingLayout->getWidgetPosition(ui->simpleOutAdvanced,
						     &pos, nullptr);
	ui->simpleStreamingLayout->insertRow(pos + 1, nullptr, simpleVodTrack);

	HookWidget(simpleVodTrack, SIGNAL(clicked(bool)),
		   SLOT(OutputsChanged()));
	connect(ui->simpleOutAdvanced, SIGNAL(toggled(bool)),
		simpleVodTrack.data(), SLOT(setVisible(bool)));

	/* -------------------------------------- */
	/* advanced output mode vod track widgets */

	vodTrackCheckbox = new QCheckBox(this);
	vodTrackCheckbox->setText(
		QTStr("Basic.Settings.Output.Adv.TwitchVodTrack"));
	vodTrackCheckbox->setLayoutDirection(Qt::RightToLeft);

	vodTrackContainer = new QWidget(this);
	QHBoxLayout *vodTrackLayout = new QHBoxLayout();
	for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
		vodTrack[i] = new QRadioButton(QString::number(i + 1));
		vodTrackLayout->addWidget(vodTrack[i]);

		HookWidget(vodTrack[i], SIGNAL(clicked(bool)),
			   SLOT(OutputsChanged()));
	}

	HookWidget(vodTrackCheckbox, SIGNAL(clicked(bool)),
		   SLOT(OutputsChanged()));

	vodTrackLayout->addStretch();
	vodTrackLayout->setContentsMargins(0, 0, 0, 0);

	vodTrackContainer->setLayout(vodTrackLayout);

	ui->advOutTopLayout->insertRow(2, vodTrackCheckbox, vodTrackContainer);

	vodTrackEnabled =
		config_get_bool(main->Config(), "AdvOut", "VodTrackEnabled");
	vodTrackCheckbox->setChecked(vodTrackEnabled);
	vodTrackContainer->setEnabled(vodTrackEnabled);

	connect(vodTrackCheckbox, SIGNAL(clicked(bool)), vodTrackContainer,
		SLOT(setEnabled(bool)));

	int trackIndex =
		config_get_int(main->Config(), "AdvOut", "VodTrackIndex");
	for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
		vodTrack[i]->setChecked((i + 1) == trackIndex);
	}
}

OBSService OBSBasicSettings::GetStream1Service()
{
	return stream1Changed ? SpawnTempService()
			      : OBSService(main->GetService());
}

void OBSBasicSettings::UpdateServiceRecommendations()
{
	bool customServer = streamUi.IsCustomService();
	ui->ignoreRecommended->setVisible(!customServer);
	ui->enforceSettingsLabel->setVisible(!customServer);

	OBSService service = GetStream1Service();

	int vbitrate, abitrate;
	BPtr<obs_service_resolution> res_list;
	size_t res_count;
	int fps;

	obs_service_get_max_bitrate(service, &vbitrate, &abitrate);
	obs_service_get_supported_resolutions(service, &res_list, &res_count);
	obs_service_get_max_fps(service, &fps);

	QString text;

#define ENFORCE_TEXT(x) QTStr("Basic.Settings.Stream.Recommended." x)
	if (vbitrate)
		text += ENFORCE_TEXT("MaxVideoBitrate")
				.arg(QString::number(vbitrate));
	if (abitrate) {
		if (!text.isEmpty())
			text += "<br>";
		text += ENFORCE_TEXT("MaxAudioBitrate")
				.arg(QString::number(abitrate));
	}
	if (res_count) {
		if (!text.isEmpty())
			text += "<br>";

		obs_service_resolution best_res = {};
		int best_res_pixels = 0;

		for (size_t i = 0; i < res_count; i++) {
			obs_service_resolution res = res_list[i];
			int res_pixels = res.cx + res.cy;
			if (res_pixels > best_res_pixels) {
				best_res = res;
				best_res_pixels = res_pixels;
			}
		}

		QString res_str =
			QString("%1x%2").arg(QString::number(best_res.cx),
					     QString::number(best_res.cy));
		text += ENFORCE_TEXT("MaxResolution").arg(res_str);
	}
	if (fps) {
		if (!text.isEmpty())
			text += "<br>";

		text += ENFORCE_TEXT("MaxFPS").arg(QString::number(fps));
	}
#undef ENFORCE_TEXT

#if YOUTUBE_ENABLED
	if (IsYouTubeService(QT_TO_UTF8(ui->service->currentText()))) {
		if (!text.isEmpty())
			text += "<br><br>";

		text += "<a href=\"https://www.youtube.com/t/terms\">"
			"YouTube Terms of Service</a><br>"
			"<a href=\"http://www.google.com/policies/privacy\">"
			"Google Privacy Policy</a><br>"
			"<a href=\"https://security.google.com/settings/security/permissions\">"
			"Google Third-Party Permissions</a>";
	}
#endif
	ui->enforceSettingsLabel->setText(text);
}

void OBSBasicSettings::DisplayEnforceWarning(bool checked)
{
	if (streamUi.IsCustomService())
		return;

	if (!checked) {
		SimpleRecordingEncoderChanged();
		return;
	}

	QMessageBox::StandardButton button;

#define ENFORCE_WARNING(x) \
	QTStr("Basic.Settings.Stream.IgnoreRecommended.Warn." x)

	button = OBSMessageBox::question(this, ENFORCE_WARNING("Title"),
					 ENFORCE_WARNING("Text"));
#undef ENFORCE_WARNING

	if (button == QMessageBox::No) {
		QMetaObject::invokeMethod(ui->ignoreRecommended, "setChecked",
					  Qt::QueuedConnection,
					  Q_ARG(bool, false));
		return;
	}

	SimpleRecordingEncoderChanged();
}

bool OBSBasicSettings::ResFPSValid(obs_service_resolution *res_list,
				   size_t res_count, int max_fps)
{
	if (!res_count && !max_fps)
		return true;

	if (res_count) {
		QString res = ui->outputResolution->currentText();
		bool found_res = false;

		int cx, cy;
		if (sscanf(QT_TO_UTF8(res), "%dx%d", &cx, &cy) != 2)
			return false;

		for (size_t i = 0; i < res_count; i++) {
			if (res_list[i].cx == cx && res_list[i].cy == cy) {
				found_res = true;
				break;
			}
		}

		if (!found_res)
			return false;
	}

	if (max_fps) {
		int fpsType = ui->fpsType->currentIndex();
		if (fpsType != 0)
			return false;

		std::string fps_str = QT_TO_UTF8(ui->fpsCommon->currentText());
		float fps;
		sscanf(fps_str.c_str(), "%f", &fps);
		if (fps > (float)max_fps)
			return false;
	}

	return true;
}

extern void set_closest_res(int &cx, int &cy,
			    struct obs_service_resolution *res_list,
			    size_t count);

/* Checks for and updates the resolution and FPS limits of a service, if any.
 *
 * If the service has a resolution and/or FPS limit, this will enforce those
 * limitations in the UI itself, preventing the user from selecting a
 * resolution or FPS that's not supported.
 *
 * This is an unpleasant thing to have to do to users, but there is no other
 * way to ensure that a service's restricted resolution/framerate values are
 * properly enforced, otherwise users will just be confused when things aren't
 * working correctly. The user can turn it off if they're partner (or if they
 * want to risk getting in trouble with their service) by selecting the "Ignore
 * recommended settings" option in the stream section of settings.
 *
 * This only affects services that have a resolution and/or framerate limit, of
 * which as of this writing, and hopefully for the foreseeable future, there is
 * only one.
 */
void OBSBasicSettings::UpdateResFPSLimits()
{
	if (loading)
		return;

	int idx = ui->service->currentIndex();
	if (idx == -1)
		return;

	bool ignoreRecommended = ui->ignoreRecommended->isChecked();
	BPtr<obs_service_resolution> res_list;
	size_t res_count = 0;
	int max_fps = 0;

	if (!streamUi.IsCustomService() && !ignoreRecommended) {
		OBSService service = GetStream1Service();
		obs_service_get_supported_resolutions(service, &res_list,
						      &res_count);
		obs_service_get_max_fps(service, &max_fps);
	}

	/* ------------------------------------ */
	/* Check for enforced res/FPS           */

	QString res = ui->outputResolution->currentText();
	QString fps_str;
	int cx = 0, cy = 0;
	double max_fpsd = (double)max_fps;
	int closest_fps_index = -1;
	double fpsd;

	sscanf(QT_TO_UTF8(res), "%dx%d", &cx, &cy);

	if (res_count)
		set_closest_res(cx, cy, res_list, res_count);

	if (max_fps) {
		int fpsType = ui->fpsType->currentIndex();

		if (fpsType == 1) { //Integer
			fpsd = (double)ui->fpsInteger->value();
		} else if (fpsType == 2) { //Fractional
			fpsd = (double)ui->fpsNumerator->value() /
			       (double)ui->fpsDenominator->value();
		} else { //Common
			sscanf(QT_TO_UTF8(ui->fpsCommon->currentText()), "%lf",
			       &fpsd);
		}

		double closest_diff = 1000000000000.0;

		for (int i = 0; i < ui->fpsCommon->count(); i++) {
			double com_fpsd;
			sscanf(QT_TO_UTF8(ui->fpsCommon->itemText(i)), "%lf",
			       &com_fpsd);

			if (com_fpsd > max_fpsd) {
				continue;
			}

			double diff = fabs(com_fpsd - fpsd);
			if (diff < closest_diff) {
				closest_diff = diff;
				closest_fps_index = i;
				fps_str = ui->fpsCommon->itemText(i);
			}
		}
	}

	QString res_str =
		QString("%1x%2").arg(QString::number(cx), QString::number(cy));

	/* ------------------------------------ */
	/* Display message box if res/FPS bad   */

	bool valid = ResFPSValid(res_list, res_count, max_fps);

	if (!valid) {
		/* if the user was already on facebook with an incompatible
		 * resolution, assume it's an upgrade */
		if (lastServiceIdx == -1 && lastIgnoreRecommended == -1) {
			ui->ignoreRecommended->setChecked(true);
			ui->ignoreRecommended->setProperty("changed", true);
			stream1Changed = true;
			EnableApplyButton(true);
			UpdateResFPSLimits();
			return;
		}

		QMessageBox::StandardButton button;

#define WARNING_VAL(x) \
	QTStr("Basic.Settings.Output.Warn.EnforceResolutionFPS." x)

		QString str;
		if (res_count)
			str += WARNING_VAL("Resolution").arg(res_str);
		if (max_fps) {
			if (!str.isEmpty())
				str += "\n";
			str += WARNING_VAL("FPS").arg(fps_str);
		}

		button = OBSMessageBox::question(this, WARNING_VAL("Title"),
						 WARNING_VAL("Msg").arg(str));
#undef WARNING_VAL

		if (button == QMessageBox::No) {
			if (idx != lastServiceIdx)
				QMetaObject::invokeMethod(
					ui->service, "setCurrentIndex",
					Qt::QueuedConnection,
					Q_ARG(int, lastServiceIdx));
			else
				QMetaObject::invokeMethod(ui->ignoreRecommended,
							  "setChecked",
							  Qt::QueuedConnection,
							  Q_ARG(bool, true));
			return;
		}
	}

	/* ------------------------------------ */
	/* Update widgets/values if switching   */
	/* to/from enforced resolution/FPS      */

	ui->outputResolution->blockSignals(true);
	if (res_count) {
		ui->outputResolution->clear();
		ui->outputResolution->setEditable(false);
		HookWidget(ui->outputResolution,
			   SIGNAL(currentIndexChanged(int)),
			   SLOT(VideoChangedResolution()));

		int new_res_index = -1;

		for (size_t i = 0; i < res_count; i++) {
			obs_service_resolution val = res_list[i];
			QString str =
				QString("%1x%2").arg(QString::number(val.cx),
						     QString::number(val.cy));
			ui->outputResolution->addItem(str);

			if (val.cx == cx && val.cy == cy)
				new_res_index = (int)i;
		}

		ui->outputResolution->setCurrentIndex(new_res_index);
		if (!valid) {
			ui->outputResolution->setProperty("changed", true);
			videoChanged = true;
			EnableApplyButton(true);
		}
	} else {
		QString baseRes = ui->baseResolution->currentText();
		int baseCX, baseCY;
		sscanf(QT_TO_UTF8(baseRes), "%dx%d", &baseCX, &baseCY);

		if (!ui->outputResolution->isEditable()) {
			RecreateOutputResolutionWidget();
			ui->outputResolution->blockSignals(true);
			ResetDownscales((uint32_t)baseCX, (uint32_t)baseCY,
					true);
			ui->outputResolution->setCurrentText(res);
		}
	}
	ui->outputResolution->blockSignals(false);

	if (max_fps) {
		for (int i = 0; i < ui->fpsCommon->count(); i++) {
			double com_fpsd;
			sscanf(QT_TO_UTF8(ui->fpsCommon->itemText(i)), "%lf",
			       &com_fpsd);

			if (com_fpsd > max_fpsd) {
				SetComboItemEnabled(ui->fpsCommon, i, false);
				continue;
			}
		}

		ui->fpsType->setCurrentIndex(0);
		ui->fpsCommon->setCurrentIndex(closest_fps_index);
		if (!valid) {
			ui->fpsType->setProperty("changed", true);
			ui->fpsCommon->setProperty("changed", true);
			videoChanged = true;
			EnableApplyButton(true);
		}
	} else {
		for (int i = 0; i < ui->fpsCommon->count(); i++)
			SetComboItemEnabled(ui->fpsCommon, i, true);
	}

	SetComboItemEnabled(ui->fpsType, 1, !max_fps);
	SetComboItemEnabled(ui->fpsType, 2, !max_fps);

	/* ------------------------------------ */

	lastIgnoreRecommended = (int)ignoreRecommended;
	lastServiceIdx = idx;
}
