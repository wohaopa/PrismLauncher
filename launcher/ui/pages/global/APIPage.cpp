// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (c) 2022 Lenny McLennington <lenny@sneed.church>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "APIPage.h"
#include "ui_APIPage.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabBar>
#include <QValidator>
#include <QVariant>

#include "Application.h"
#include "BuildConfig.h"
#include "meta/Index.h"
#include "meta/Property.h"
#include "net/PasteUpload.h"
#include "settings/SettingsObject.h"
#include "tools/BaseProfiler.h"

APIPage::APIPage(QWidget* parent) : QWidget(parent), ui(new Ui::APIPage)
{
    // This is here so you can reorder the entries in the combobox without messing stuff up
    int comboBoxEntries[] = { PasteUpload::PasteType::Mclogs, PasteUpload::PasteType::NullPointer, PasteUpload::PasteType::PasteGG,
                              PasteUpload::PasteType::Hastebin };

    static QRegularExpression validUrlRegExp("https?://.+");
    static QRegularExpression validMSAClientID(
        QRegularExpression::anchoredPattern("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}"));
    static QRegularExpression validFlameKey(QRegularExpression::anchoredPattern("\\$2[ayb]\\$.{56}"));

    ui->setupUi(this);

    for (auto pasteType : comboBoxEntries) {
        ui->pasteTypeComboBox->addItem(PasteUpload::PasteTypes.at(pasteType).name, pasteType);
    }

    void (QComboBox::*currentIndexChangedSignal)(int)(&QComboBox::currentIndexChanged);
    connect(ui->pasteTypeComboBox, currentIndexChangedSignal, this, &APIPage::updateBaseURLPlaceholder);
    // This function needs to be called even when the ComboBox's index is still in its default state.
    updateBaseURLPlaceholder(ui->pasteTypeComboBox->currentIndex());
    // NOTE: this allows http://, but we replace that with https later anyway
    ui->metaURL->setValidator(new QRegularExpressionValidator(validUrlRegExp, ui->metaURL));
    ui->resourceURL->setValidator(new QRegularExpressionValidator(validUrlRegExp, ui->resourceURL));
    ui->librariesURL->setValidator(new QRegularExpressionValidator(validUrlRegExp, ui->librariesURL));
    ui->baseURLEntry->setValidator(new QRegularExpressionValidator(validUrlRegExp, ui->baseURLEntry));
    ui->msaClientID->setValidator(new QRegularExpressionValidator(validMSAClientID, ui->msaClientID));
    ui->flameKey->setValidator(new QRegularExpressionValidator(validFlameKey, ui->flameKey));

    ui->metaURL->setPlaceholderText(BuildConfig.META_URL);
    ui->resourceURL->setPlaceholderText(BuildConfig.RESOURCE_BASE);
    ui->librariesURL->setPlaceholderText(BuildConfig.LIBRARY_BASE);
    ui->userAgentLineEdit->setPlaceholderText(BuildConfig.USER_AGENT);

    loadSettings();

    resetBaseURLNote();
    connect(ui->pasteTypeComboBox, currentIndexChangedSignal, this, &APIPage::updateBaseURLNote);
    connect(ui->baseURLEntry, &QLineEdit::textEdited, this, &APIPage::resetBaseURLNote);
    connect(APPLICATION->metadataIndex()->property().get(), &Meta::Property::succeededApplyProperties,
            [&](const QHash<QString, QString> succeed) {
                QString context;
                for (auto& item : succeed.keys())
                    context.append("\n").append(item).append(": ").append(succeed[item]);

                QMessageBox::information(nullptr, tr("OK"),
                                         tr("The following meta server properties were successfully obtained: %1").arg(context));
                loadSettings();
                ui->applyPropertiesBtn->setEnabled(true);
                ui->applyPropertiesBtn->setText(tr("Download and Apply Properties in the Meta Server"));
            });
    connect(APPLICATION->metadataIndex()->property().get(), &Meta::Property::failedApplyProperties, [&](const QString reasons) {
        QMessageBox::warning(nullptr, tr("FAILED"),
                             tr("Unable to download the properties file from the \n%1\bReasons:%2")
                                 .arg(APPLICATION->metadataIndex()->property()->url().toString())
                                 .arg(reasons));
        ui->applyPropertiesBtn->setEnabled(true);
        ui->applyPropertiesBtn->setText(tr("Download and Apply Properties in the Meta Server"));
    });
}

APIPage::~APIPage()
{
    delete ui;
}

void APIPage::resetBaseURLNote()
{
    ui->baseURLNote->hide();
    baseURLPasteType = ui->pasteTypeComboBox->currentIndex();
}

void APIPage::updateBaseURLNote(int index)
{
    if (baseURLPasteType == index) {
        ui->baseURLNote->hide();
    } else if (!ui->baseURLEntry->text().isEmpty()) {
        ui->baseURLNote->show();
    }
}

void APIPage::updateBaseURLPlaceholder(int index)
{
    int pasteType = ui->pasteTypeComboBox->itemData(index).toInt();
    QString pasteDefaultURL = PasteUpload::PasteTypes.at(pasteType).defaultBase;
    ui->baseURLEntry->setPlaceholderText(pasteDefaultURL);
}

void APIPage::loadSettings()
{
    auto s = APPLICATION->settings();

    int pasteType = s->get("PastebinType").toInt();
    QString pastebinURL = s->get("PastebinCustomAPIBase").toString();

    ui->baseURLEntry->setText(pastebinURL);
    int pasteTypeIndex = ui->pasteTypeComboBox->findData(pasteType);
    if (pasteTypeIndex == -1) {
        pasteTypeIndex = ui->pasteTypeComboBox->findData(PasteUpload::PasteType::Mclogs);
        ui->baseURLEntry->clear();
    }

    ui->pasteTypeComboBox->setCurrentIndex(pasteTypeIndex);

    QString msaClientID = s->get("MSAClientIDOverride").toString();
    ui->msaClientID->setText(msaClientID);
    QString metaURL = s->get("MetaURLOverride").toString();
    ui->metaURL->setText(metaURL);
    QString resourceURL = s->get("MinecraftResourceURLOverride").toString();
    ui->resourceURL->setText(resourceURL);
    QString librariesURL = s->get("MinecraftLibrariesURLOverride").toString();
    ui->librariesURL->setText(librariesURL);
    QString flameKey = s->get("FlameKeyOverride").toString();
    ui->flameKey->setText(flameKey);
    QString modrinthToken = s->get("ModrinthToken").toString();
    ui->modrinthToken->setText(modrinthToken);
    QString customUserAgent = s->get("UserAgentOverride").toString();
    ui->userAgentLineEdit->setText(customUserAgent);
}

QString verifyUrl(const QString& url)
{
    QUrl qUrl(url);
    // Add required trailing slash
    if (!qUrl.isEmpty() && !qUrl.path().endsWith('/')) {
        QString path = qUrl.path();
        path.append('/');
        qUrl.setPath(path);
    }
    // HTTP may not be allowed either?
    if (!qUrl.isEmpty() && qUrl.scheme() == "http") {
        qUrl.setScheme("https");
    }
    return qUrl.toString();
}

void APIPage::applySettings()
{
    auto s = APPLICATION->settings();

    s->set("PastebinType", ui->pasteTypeComboBox->currentData().toInt());
    s->set("PastebinCustomAPIBase", ui->baseURLEntry->text());

    QString msaClientID = ui->msaClientID->text();
    s->set("MSAClientIDOverride", msaClientID);
    s->set("MetaURLOverride", verifyUrl(ui->metaURL->text()));
    s->set("MinecraftResourceURLOverride", verifyUrl(ui->resourceURL->text()));
    s->set("MinecraftLibrariesURLOverride", verifyUrl(ui->librariesURL->text()));
    QString flameKey = ui->flameKey->text();
    s->set("FlameKeyOverride", flameKey);
    QString modrinthToken = ui->modrinthToken->text();
    s->set("ModrinthToken", modrinthToken);
    s->set("UserAgentOverride", ui->userAgentLineEdit->text());
}

bool APIPage::apply()
{
    applySettings();
    return true;
}

void APIPage::retranslate()
{
    ui->retranslateUi(this);
}

void APIPage::on_applyPropertiesBtn_clicked()
{
    if (ui->applyPropertiesBtn->isEnabled()) {
        ui->applyPropertiesBtn->setText(tr("Downloading and Applying..."));
        ui->applyPropertiesBtn->setEnabled(false);
        APPLICATION->metadataIndex()->property()->downloadAndApplyProperties();
    }
}
