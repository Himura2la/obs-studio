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

#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QPointer>
#include <QSplitter>
#include <QTabWidget>
#include "qt-display.hpp"
#include <obs.hpp>
#include "window-basic-filters.hpp"
#include "window-basic-transform.hpp"

enum class PropertiesType {
	Scene,
	Source,
	Transition
};

class OBSPropertiesView;
class OBSBasic;

class OBSBasicProperties : public QDialog {
	Q_OBJECT

private:
	QPointer<OBSQTDisplay> preview;

	OBSBasic            *main;
	bool                acceptClicked;
	enum PropertiesType propType;

	OBSSource  source;
	OBSSignal  removedSignal;
	OBSSignal  renamedSignal;
	OBSSignal  updatePropertiesSignal;
	OBSData    oldSettings;
	OBSPropertiesView *view;
	OBSBasicFilters *filters;
	OBSBasicTransform *transformWindow;
	QDialogButtonBox *buttonBox;
	QSplitter *windowSplitter;
	QTabWidget *tabs;
	QWidget *HotkeysTab(QWidget *parent);
	QWidget *PerSceneTransitionWidget(QWidget *parent);
	QWidget *AdvancedTab(QWidget *parent);
	QComboBox *combo;
	QSpinBox *duration;
	QComboBox *deinterlace;
	QComboBox *sf;
	QComboBox *order;

	static void SourceRemoved(void *data, calldata_t *params);
	static void SourceRenamed(void *data, calldata_t *params);
	static void UpdateProperties(void *data, calldata_t *params);
	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	bool ConfirmQuit();
	int  CheckSettings();
	void Cleanup();

private slots:
	void on_buttonBox_clicked(QAbstractButton *button);
	void SetDeinterlacingMode(int index);
	void SetDeinterlacingOrder(int index);
	void SetScaleFilter(int index);

public:
	OBSBasicProperties(QWidget *parent, OBSSource source_,
			PropertiesType type);
	~OBSBasicProperties();

	void Init();
	void SetTabIndex(int tab);

protected:
	virtual void closeEvent(QCloseEvent *event) override;
	virtual void reject() override;
};
