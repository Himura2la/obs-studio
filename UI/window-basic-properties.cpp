/******************************************************************************
    Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs-app.hpp"
#include "window-basic-properties.hpp"
#include "window-basic-main.hpp"
#include "qt-wrappers.hpp"
#include "display-helpers.hpp"
#include "properties-view.hpp"

#include <QCloseEvent>
#include <QScreen>
#include <QWindow>
#include <QMessageBox>

using namespace std;

OBSBasicProperties::OBSBasicProperties(QWidget *parent, OBSSource source_,
			PropertiesType type)
	: QDialog                (parent),
	  preview                (new OBSQTDisplay(this)),
	  main                   (qobject_cast<OBSBasic*>(parent)),
	  acceptClicked          (false),
	  source                 (source_),
	  removedSignal          (obs_source_get_signal_handler(source),
	                          "remove", OBSBasicProperties::SourceRemoved,
	                          this),
	  renamedSignal          (obs_source_get_signal_handler(source),
	                          "rename", OBSBasicProperties::SourceRenamed,
	                          this),
	  oldSettings            (obs_data_create()),
	  buttonBox              (new QDialogButtonBox(this))
{
	int cx = (int)config_get_int(App()->GlobalConfig(), "PropertiesWindow",
			"cx");
	int cy = (int)config_get_int(App()->GlobalConfig(), "PropertiesWindow",
			"cy");

	buttonBox->setObjectName(QStringLiteral("buttonBox"));
	buttonBox->setStandardButtons(QDialogButtonBox::Ok |
	                              QDialogButtonBox::Cancel |
	                              QDialogButtonBox::RestoreDefaults);

	buttonBox->button(QDialogButtonBox::Ok)->setText(QTStr("OK"));
	buttonBox->button(QDialogButtonBox::Cancel)->setText(QTStr("Cancel"));
	buttonBox->button(QDialogButtonBox::RestoreDefaults)->
		setText(QTStr("Defaults"));

	if (cx > 400 && cy > 400)
		resize(cx, cy);
	else
		resize(720, 580);

	QMetaObject::connectSlotsByName(this);

	/* The OBSData constructor increments the reference once */
	obs_data_release(oldSettings);

	OBSData settings = obs_source_get_settings(source);
	obs_data_apply(oldSettings, settings);
	obs_data_release(settings);

	tabs = new QTabWidget();

	if (type != PropertiesType::Scene) {
		view = new OBSPropertiesView(settings, source,
				(PropertiesReloadCallback)obs_source_properties,
				(PropertiesUpdateCallback)obs_source_update);
		view->setMinimumHeight(150);

		tabs->addTab(view, QTStr("Basic.Settings.General"));
	}

	filters = new OBSBasicFilters(this, source);
	filters->Init();
	tabs->addTab(filters, QTStr("Filters"));

	if (type == PropertiesType::Source) {
		transformWindow = new OBSBasicTransform(this,
				main->GetCurrentScene());

		tabs->addTab(transformWindow,
				QTStr("Basic.MainMenu.Edit.Transform"));
	}

	if (type != PropertiesType::Transition)
		tabs->addTab(HotkeysTab(this), QTStr("Basic.Settings.Hotkeys"));

	if (type == PropertiesType::Scene)
		tabs->addTab(PerSceneTransitionWidget(this),
				QTStr("TransitionOverride"));

	if (type == PropertiesType::Source)
		tabs->addTab(AdvancedTab(this),
				QTStr("Basic.Settings.Advanced"));

	preview->setMinimumSize(20, 150);
	preview->setSizePolicy(QSizePolicy(QSizePolicy::Expanding,
				QSizePolicy::Expanding));

	// Create a QSplitter to keep a unified workflow here.
	windowSplitter = new QSplitter(Qt::Orientation::Vertical, this);
	windowSplitter->addWidget(preview);
	windowSplitter->addWidget(tabs);
	windowSplitter->setChildrenCollapsible(false);
	//windowSplitter->setSizes(QList<int>({ 16777216, 150 }));
	windowSplitter->setStretchFactor(0, 3);
	windowSplitter->setStretchFactor(1, 1);

	setLayout(new QVBoxLayout(this));
	layout()->addWidget(windowSplitter);
	layout()->addWidget(buttonBox);
	layout()->setAlignment(buttonBox, Qt::AlignBottom);

	tabs->show();
	installEventFilter(CreateShortcutFilter());

	const char *name = obs_source_get_name(source);
	setWindowTitle(QTStr("Basic.PropertiesWindow").arg(QT_UTF8(name)));

	obs_source_inc_showing(source);

	updatePropertiesSignal.Connect(obs_source_get_signal_handler(source),
			"update_properties",
			OBSBasicProperties::UpdateProperties,
			this);

	auto addDrawCallback = [this] ()
	{
		obs_display_add_draw_callback(preview->GetDisplay(),
				OBSBasicProperties::DrawPreview, this);
	};

	enum obs_source_type sourceType = obs_source_get_type(source);
	uint32_t caps = obs_source_get_output_flags(source);
	bool drawable_type = sourceType == OBS_SOURCE_TYPE_INPUT ||
		sourceType == OBS_SOURCE_TYPE_SCENE;

	if (drawable_type && (caps & OBS_SOURCE_VIDEO) != 0)
		connect(preview.data(), &OBSQTDisplay::DisplayCreated,
				addDrawCallback);

	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_VIDEO) == 0) {
		tabs->setTabEnabled(2, false);
		tabs->setTabEnabled(4, false);
	}

	propType = type;
}

OBSBasicProperties::~OBSBasicProperties()
{
	obs_source_dec_showing(source);
	main->SaveProject();
}

void OBSBasicProperties::SourceRemoved(void *data, calldata_t *params)
{
	QMetaObject::invokeMethod(static_cast<OBSBasicProperties*>(data),
			"close");

	UNUSED_PARAMETER(params);
}

void OBSBasicProperties::SourceRenamed(void *data, calldata_t *params)
{
	const char *name = calldata_string(params, "new_name");
	QString title = QTStr("Basic.PropertiesWindow").arg(QT_UTF8(name));

	QMetaObject::invokeMethod(static_cast<OBSBasicProperties*>(data),
	                "setWindowTitle", Q_ARG(QString, title));
}

void OBSBasicProperties::UpdateProperties(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<OBSBasicProperties*>(data)->view,
			"ReloadProperties");
}

void OBSBasicProperties::on_buttonBox_clicked(QAbstractButton *button)
{
	QDialogButtonBox::ButtonRole val = buttonBox->buttonRole(button);

	if (val == QDialogButtonBox::AcceptRole) {
		acceptClicked = true;

		if (view && propType != PropertiesType::Scene) {
			if (view->DeferUpdate())
				view->UpdateSettings();
		}

		close();

	} else if (val == QDialogButtonBox::RejectRole) {
		if (view && propType != PropertiesType::Scene) {
			obs_data_t *settings = obs_source_get_settings(source);
			obs_data_clear(settings);
			obs_data_release(settings);

			if (view->DeferUpdate())
				obs_data_apply(settings, oldSettings);
			else
				obs_source_update(source, oldSettings);
		}

		close();

	} else if (val == QDialogButtonBox::ResetRole) {
		if (tabs->currentIndex() == 0 &&
				propType == PropertiesType::Source) {
			obs_data_t *settings = obs_source_get_settings(source);
			obs_data_clear(settings);
			obs_data_release(settings);

			if (!view->DeferUpdate())
				obs_source_update(source, nullptr);

			view->RefreshProperties();
		} else if (tabs->currentIndex() == 1 ||
				(tabs->currentIndex() == 0 &&
				propType == PropertiesType::Scene)) {
			filters->ResetFilters();
		} else if (tabs->currentIndex() == 2 &&
				propType == PropertiesType::Source) {
			transformWindow->ResetTransform();
		} else if (tabs->currentIndex() == 2 &&
				propType == PropertiesType::Scene) {
			combo->setCurrentIndex(0);
			duration->setValue(300);
		} else if (tabs->currentIndex() == 4 &&
				propType == PropertiesType::Source) {
			if (deinterlace)
				deinterlace->setCurrentIndex(0);
			if (order)
				order->setCurrentIndex(0);
			sf->setCurrentIndex(0);
		}
	}
}

void OBSBasicProperties::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	OBSBasicProperties *window = static_cast<OBSBasicProperties*>(data);

	if (!window->source)
		return;

	uint32_t sourceCX = max(obs_source_get_width(window->source), 1u);
	uint32_t sourceCY = max(obs_source_get_height(window->source), 1u);

	int   x, y;
	int   newCX, newCY;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY),
			-100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);

	obs_source_video_render(window->source);

	gs_projection_pop();
	gs_viewport_pop();
}

void OBSBasicProperties::Cleanup()
{
	config_set_int(App()->GlobalConfig(), "PropertiesWindow", "cx",
			width());
	config_set_int(App()->GlobalConfig(), "PropertiesWindow", "cy",
			height());

	obs_display_remove_draw_callback(preview->GetDisplay(),
		OBSBasicProperties::DrawPreview, this);
}

void OBSBasicProperties::reject()
{
	if (!acceptClicked && (CheckSettings() != 0)) {
		if (!ConfirmQuit()) {
			return;
		}
	}

	Cleanup();
	done(0);
}

void OBSBasicProperties::closeEvent(QCloseEvent *event)
{
	if (!acceptClicked && (CheckSettings() != 0)) {
		if (!ConfirmQuit()) {
			event->ignore();
			return;
		}
	}

	QDialog::closeEvent(event);
	if (!event->isAccepted())
		return;

	Cleanup();
}

void OBSBasicProperties::Init()
{
	show();
}

int OBSBasicProperties::CheckSettings()
{
	OBSData currentSettings = obs_source_get_settings(source);
	const char *oldSettingsJson = obs_data_get_json(oldSettings);
	const char *currentSettingsJson = obs_data_get_json(currentSettings);

	int ret = strcmp(currentSettingsJson, oldSettingsJson);

	obs_data_release(currentSettings);
	return ret;
}

bool OBSBasicProperties::ConfirmQuit()
{
	QMessageBox::StandardButton button;

	button = OBSMessageBox::question(this,
			QTStr("Basic.PropertiesWindow.ConfirmTitle"),
			QTStr("Basic.PropertiesWindow.Confirm"),
			QMessageBox::Save | QMessageBox::Discard |
			QMessageBox::Cancel);

	switch (button) {
	case QMessageBox::Save:
		acceptClicked = true;
		if (view->DeferUpdate())
			view->UpdateSettings();
		// Do nothing because the settings are already updated
		break;
	case QMessageBox::Discard:
		obs_source_update(source, oldSettings);
		break;
	case QMessageBox::Cancel:
		return false;
		break;
	default:
		/* If somehow the dialog fails to show, just default to
		 * saving the settings. */
		break;
	}
	return true;
}

void OBSBasicProperties::SetTabIndex(int tab)
{
	tabs->setCurrentIndex(tab);
}

QWidget *OBSBasicProperties::PerSceneTransitionWidget(QWidget *parent)
{
	OBSSource scene = main->GetCurrentSceneSource();
	OBSData data = obs_source_get_private_settings(scene);
	obs_data_release(data);

	obs_data_set_default_int(data, "transition_duration", 300);
	const char *curTransition = obs_data_get_string(data, "transition");
	int curDuration = (int)obs_data_get_int(data, "transition_duration");

	QWidget *w = new QWidget(parent);
	combo = new QComboBox(w);
	duration = new QSpinBox(w);
	QLabel *trLabel = new QLabel(QTStr("Transition"));
	QLabel *durationLabel = new QLabel(QTStr("Basic.TransitionDuration"));

	duration->setMinimum(50);
	duration->setSuffix("ms");
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(curDuration);

	QFormLayout *layout = new QFormLayout();
	layout->setLabelAlignment(Qt::AlignRight);

	layout->addRow(trLabel, combo);
	layout->addRow(durationLabel, duration);

	combo->addItem("None");

	const char *name = nullptr;

	for (int i = 0; i < main->ui->transitions->count(); i++) {
		OBSSource tr;
		tr = main->GetTransitionComboItem(main->ui->transitions, i);
		name = obs_source_get_name(tr);

		combo->addItem(name);
	}

	int index = combo->findText(curTransition);
	if (index != -1) {
		combo->setCurrentIndex(index);
	}

	auto setTransition = [this] (int idx)
	{
		OBSSource scene = main->GetCurrentSceneSource();
		OBSData data = obs_source_get_private_settings(scene);
		obs_data_release(data);

		if (idx == -1) {
			obs_data_set_string(data, "transition", "");
			return;
		}

		OBSSource tr = main->GetTransitionComboItem(
				main->ui->transitions, idx - 1);
		const char *name = obs_source_get_name(tr);

		obs_data_set_string(data, "transition", name);
	};

	auto setDuration = [this] (int duration)
	{
		OBSSource scene = main->GetCurrentSceneSource();
		OBSData data = obs_source_get_private_settings(scene);
		obs_data_release(data);

		obs_data_set_int(data, "transition_duration", duration);
	};

	connect(combo, (void
			(QComboBox::*)(int))&QComboBox::currentIndexChanged,
			setTransition);
	connect(duration, (void (QSpinBox::*)(int))&QSpinBox::valueChanged,
			setDuration);

	w->setLayout(layout);
	return w;
}

void OBSBasicProperties::SetDeinterlacingMode(int index)
{
	if (index == 0)
		order->setEnabled(false);
	else
		order->setEnabled(true);

	obs_deinterlace_mode mode = (obs_deinterlace_mode)(index);

	obs_source_set_deinterlace_mode(source, mode);
}

void OBSBasicProperties::SetDeinterlacingOrder(int index)
{
	obs_deinterlace_field_order deinterlaceOrder =
			(obs_deinterlace_field_order)(index);

	obs_source_set_deinterlace_field_order(source, deinterlaceOrder);
}

void OBSBasicProperties::SetScaleFilter(int index)
{
	obs_scale_type mode = (obs_scale_type)(index);
	OBSSceneItem sceneItem = main->GetCurrentSceneItem();

	obs_sceneitem_set_scale_filter(sceneItem, mode);
}

QWidget *OBSBasicProperties::AdvancedTab(QWidget *parent)
{
	uint32_t flags = obs_source_get_output_flags(source);
	bool isAsyncVideo = (flags & OBS_SOURCE_ASYNC_VIDEO) ==
			OBS_SOURCE_ASYNC_VIDEO;

	obs_deinterlace_mode deinterlaceMode =
		obs_source_get_deinterlace_mode(source);
	obs_deinterlace_field_order deinterlaceOrder =
		obs_source_get_deinterlace_field_order(source);
	obs_scale_type scaleFilter = obs_sceneitem_get_scale_filter(
			main->GetCurrentSceneItem());

	QWidget *w = new QWidget(parent);

	sf = new QComboBox(w);
	deinterlace = new QComboBox(w);
	order = new QComboBox(w);

	QLabel *deinterlaceLabel = new QLabel(QTStr("Deinterlacing.Mode"));
	QLabel *orderLabel = new QLabel(QTStr("Deinterlacing.Order"));
	QLabel *sfLabel = new QLabel(QTStr("ScaleFiltering"));

	QFormLayout *layout = new QFormLayout();
	layout->setLabelAlignment(Qt::AlignRight);

	if (isAsyncVideo) {
#define ADD_MODE(name, mode) \
	deinterlace->addItem(QTStr("" name)); \
	deinterlace->setProperty("mode", (int)mode);

		ADD_MODE("Disable",
				OBS_DEINTERLACE_MODE_DISABLE);
		ADD_MODE("Deinterlacing.Discard",
				OBS_DEINTERLACE_MODE_DISCARD);
		ADD_MODE("Deinterlacing.Retro",
				OBS_DEINTERLACE_MODE_RETRO);
		ADD_MODE("Deinterlacing.Blend",
				OBS_DEINTERLACE_MODE_BLEND);
		ADD_MODE("Deinterlacing.Blend2x",
				OBS_DEINTERLACE_MODE_BLEND_2X);
		ADD_MODE("Deinterlacing.Linear",
				OBS_DEINTERLACE_MODE_LINEAR);
		ADD_MODE("Deinterlacing.Linear2x",
				OBS_DEINTERLACE_MODE_LINEAR_2X);
		ADD_MODE("Deinterlacing.Yadif",
				OBS_DEINTERLACE_MODE_YADIF);
		ADD_MODE("Deinterlacing.Yadif2x",
				OBS_DEINTERLACE_MODE_YADIF_2X);
#undef ADD_MODE

		connect(deinterlace, SIGNAL(currentIndexChanged(int)),
				this, SLOT(SetDeinterlacingMode(int)));
		connect(order, SIGNAL(currentIndexChanged(int)),
				this, SLOT(SetDeinterlacingOrder(int)));

#define ADD_ORDER(name, mode) \
	order->addItem(QTStr("Deinterlacing." name)); \
	order->setProperty("order", (int)mode);

		ADD_ORDER("TopFieldFirst",
				OBS_DEINTERLACE_FIELD_ORDER_TOP);
		ADD_ORDER("BottomFieldFirst",
				OBS_DEINTERLACE_FIELD_ORDER_BOTTOM);
#undef ADD_ORDER

		layout->addRow(deinterlaceLabel, deinterlace);
		layout->addRow(orderLabel, order);

		if (((int)deinterlaceMode) == 0)
			order->setEnabled(false);
		else
			order->setEnabled(true);

		deinterlace->setCurrentIndex((int)deinterlaceMode);
		order->setCurrentIndex((int)deinterlaceOrder);
	} else {
		delete deinterlace;
		deinterlace = nullptr;

		delete order;
		order = nullptr;
	}

#define ADD_SF_MODE(name, mode) \
	sf->addItem(QTStr("" name)); \
	sf->setProperty("mode", (int)mode);

	ADD_SF_MODE("Disable",                 OBS_SCALE_DISABLE);
	ADD_SF_MODE("ScaleFiltering.Point",    OBS_SCALE_POINT);
	ADD_SF_MODE("ScaleFiltering.Bilinear", OBS_SCALE_BILINEAR);
	ADD_SF_MODE("ScaleFiltering.Bicubic",  OBS_SCALE_BICUBIC);
	ADD_SF_MODE("ScaleFiltering.Lanczos",  OBS_SCALE_LANCZOS);
#undef ADD_SF_MODE

	sf->setCurrentIndex((int)scaleFilter);

	connect(sf, SIGNAL(currentIndexChanged(int)),
			this, SLOT(SetScaleFilter(int)));

	layout->addRow(sfLabel, sf);

	w->setLayout(layout);
	return w;
}

QWidget *OBSBasicProperties::HotkeysTab(QWidget *parent)
{
	QWidget *w = new QWidget(parent);
	return w;
}
