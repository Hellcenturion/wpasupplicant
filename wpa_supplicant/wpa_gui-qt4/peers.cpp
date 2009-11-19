/*
 * wpa_gui - Peers class
 * Copyright (c) 2009, Atheros Communications
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include <cstdio>
#include <QImageReader>
#include <QMessageBox>

#include "wpa_ctrl.h"
#include "wpagui.h"
#include "stringquery.h"
#include "peers.h"


static const int peer_role_address = Qt::UserRole + 1;
static const int peer_role_type = Qt::UserRole + 2;
static const int peer_role_uuid = Qt::UserRole + 3;

/*
 * TODO:
 * - add current AP info (e.g., from WPS) in station mode
 * - different icons to indicate peer type
 */

enum peer_type {
	PEER_TYPE_ASSOCIATED_STATION,
	PEER_TYPE_AP,
	PEER_TYPE_AP_WPS,
	PEER_TYPE_WPS_PIN_NEEDED,
	PEER_TYPE_WPS_ER_AP,
	PEER_TYPE_WPS_ER_ENROLLEE
};


Peers::Peers(QWidget *parent, const char *, bool, Qt::WFlags)
	: QDialog(parent)
{
	setupUi(this);

	if (QImageReader::supportedImageFormats().contains(QByteArray("svg")))
	{
		default_icon = new QIcon(":/icons/wpa_gui.svg");
		ap_icon = new QIcon(":/icons/ap.svg");
		laptop_icon = new QIcon(":/icons/laptop.svg");
	} else {
		default_icon = new QIcon(":/icons/wpa_gui.png");
		ap_icon = new QIcon(":/icons/ap.png");
		laptop_icon = new QIcon(":/icons/laptop.png");
	}

	peers->setModel(&model);
	peers->setResizeMode(QListView::Adjust);

	peers->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(peers, SIGNAL(customContextMenuRequested(const QPoint &)),
		this, SLOT(context_menu(const QPoint &)));

	wpagui = NULL;
}


void Peers::setWpaGui(WpaGui *_wpagui)
{
	wpagui = _wpagui;
	update_peers();
}


Peers::~Peers()
{
	delete default_icon;
	delete ap_icon;
	delete laptop_icon;
}


void Peers::languageChange()
{
	retranslateUi(this);
}


void Peers::context_menu(const QPoint &pos)
{
	QMenu *menu = new QMenu;
	if (menu == NULL)
		return;

	QModelIndex idx = peers->indexAt(pos);
	if (idx.isValid()) {
		ctx_item = model.itemFromIndex(idx);
		int type = ctx_item->data(peer_role_type).toInt();
		QString title;
		switch (type) {
		case PEER_TYPE_ASSOCIATED_STATION:
			title = tr("Associated station");
			break;
		case PEER_TYPE_AP:
			title = tr("AP");
			break;
		case PEER_TYPE_AP_WPS:
			title = tr("WPS AP");
			break;
		case PEER_TYPE_WPS_PIN_NEEDED:
			title = tr("WPS PIN needed");
			break;
		case PEER_TYPE_WPS_ER_AP:
			title = tr("ER: WPS AP");
			break;
		case PEER_TYPE_WPS_ER_ENROLLEE:
			title = tr("ER: WPS Enrollee");
			break;
		}
		menu->addAction(title)->setEnabled(false);
		menu->addSeparator();

		if (type == PEER_TYPE_ASSOCIATED_STATION ||
		    type == PEER_TYPE_AP_WPS ||
		    type == PEER_TYPE_WPS_PIN_NEEDED ||
		    type == PEER_TYPE_WPS_ER_ENROLLEE) {
			/* TODO: only for peers that are requesting WPS PIN
			 * method */
			menu->addAction(QString("Enter WPS PIN"), this,
					SLOT(enter_pin()));
		}
	} else {
		ctx_item = NULL;
		menu->addAction(QString("Refresh"), this, SLOT(ctx_refresh()));
	}

	menu->exec(peers->mapToGlobal(pos));
}


void Peers::enter_pin()
{
	if (ctx_item == NULL)
		return;

	int peer_type = ctx_item->data(peer_role_type).toInt();
	QString uuid;
	QString addr;
	if (peer_type == PEER_TYPE_WPS_ER_ENROLLEE)
		uuid = ctx_item->data(peer_role_uuid).toString();
	else
		addr = ctx_item->data(peer_role_address).toString();

	StringQuery input(tr("PIN:"));
	input.setWindowTitle(tr("PIN for ") + ctx_item->text());
	if (input.exec() != QDialog::Accepted)
		return;

	char cmd[100];
	char reply[100];
	size_t reply_len;

	if (peer_type == PEER_TYPE_WPS_ER_ENROLLEE) {
		snprintf(cmd, sizeof(cmd), "WPS_ER_PIN %s %s",
			 uuid.toAscii().constData(),
			 input.get_string().toAscii().constData());
	} else {
		snprintf(cmd, sizeof(cmd), "WPS_PIN %s %s",
			 addr.toAscii().constData(),
			 input.get_string().toAscii().constData());
	}
	reply_len = sizeof(reply) - 1;
	if (wpagui->ctrlRequest(cmd, reply, &reply_len) < 0) {
		QMessageBox msg;
		msg.setIcon(QMessageBox::Warning);
		msg.setText("Failed to set the WPS PIN.");
		msg.exec();
	}
}


void Peers::ctx_refresh()
{
	update_peers();
}


void Peers::add_station(QString info)
{
	QStringList lines = info.split(QRegExp("\\n"));
	QString name;

	for (QStringList::Iterator it = lines.begin();
	     it != lines.end(); it++) {
		int pos = (*it).indexOf('=') + 1;
		if (pos < 1)
			continue;

		if ((*it).startsWith("wpsDeviceName="))
			name = (*it).mid(pos);
	}

	if (name.isEmpty())
		name = lines[0];

	QStandardItem *item = new QStandardItem(*laptop_icon, name);
	if (item) {
		item->setData(lines[0], peer_role_address);
		item->setData(PEER_TYPE_ASSOCIATED_STATION,
			      peer_role_type);
		item->setToolTip(info);
		model.appendRow(item);
	}
}


void Peers::add_stations()
{
	char reply[2048];
	size_t reply_len;
	char cmd[30];
	int res;

	reply_len = sizeof(reply) - 1;
	if (wpagui->ctrlRequest("STA-FIRST", reply, &reply_len) < 0)
		return;

	do {
		reply[reply_len] = '\0';
		QString info(reply);
		char *txt = reply;
		while (*txt != '\0' && *txt != '\n')
			txt++;
		*txt++ = '\0';
		if (strncmp(reply, "FAIL", 4) == 0 ||
		    strncmp(reply, "UNKNOWN", 7) == 0)
			break;

		add_station(info);

		reply_len = sizeof(reply) - 1;
		snprintf(cmd, sizeof(cmd), "STA-NEXT %s", reply);
		res = wpagui->ctrlRequest(cmd, reply, &reply_len);
	} while (res >= 0);
}


void Peers::add_single_station(const char *addr)
{
	char reply[2048];
	size_t reply_len;
	char cmd[30];

	reply_len = sizeof(reply) - 1;
	snprintf(cmd, sizeof(cmd), "STA %s", addr);
	if (wpagui->ctrlRequest(cmd, reply, &reply_len) < 0)
		return;

	reply[reply_len] = '\0';
	QString info(reply);
	char *txt = reply;
	while (*txt != '\0' && *txt != '\n')
		txt++;
	*txt++ = '\0';
	if (strncmp(reply, "FAIL", 4) == 0 ||
	    strncmp(reply, "UNKNOWN", 7) == 0)
		return;

	add_station(info);
}


void Peers::add_scan_results()
{
	char reply[2048];
	size_t reply_len;
	int index;
	char cmd[20];

	index = 0;
	while (wpagui) {
		snprintf(cmd, sizeof(cmd), "BSS %d", index++);
		if (index > 1000)
			break;

		reply_len = sizeof(reply) - 1;
		if (wpagui->ctrlRequest(cmd, reply, &reply_len) < 0)
			break;
		reply[reply_len] = '\0';

		QString bss(reply);
		if (bss.isEmpty() || bss.startsWith("FAIL"))
			break;

		QString ssid, bssid, flags, wps_name;

		QStringList lines = bss.split(QRegExp("\\n"));
		for (QStringList::Iterator it = lines.begin();
		     it != lines.end(); it++) {
			int pos = (*it).indexOf('=') + 1;
			if (pos < 1)
				continue;

			if ((*it).startsWith("bssid="))
				bssid = (*it).mid(pos);
			else if ((*it).startsWith("flags="))
				flags = (*it).mid(pos);
			else if ((*it).startsWith("ssid="))
				ssid = (*it).mid(pos);
			else if ((*it).startsWith("wps_device_name="))
				wps_name = (*it).mid(pos);
		}

		QString name = wps_name;
		if (name.isEmpty())
			name = ssid + "\n" + bssid;

		QStandardItem *item = new QStandardItem(*ap_icon, name);
		if (item) {
			item->setData(bssid, peer_role_address);
			if (flags.contains("[WPS"))
				item->setData(PEER_TYPE_AP_WPS,
					      peer_role_type);
			else
				item->setData(PEER_TYPE_AP, peer_role_type);

			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].length() > 60) {
					lines[i].remove(
						60, lines[i].length());
					lines[i] += "..";
				}
			}
			item->setToolTip(lines.join("\n"));
			model.appendRow(item);
		}
	}
}


void Peers::update_peers()
{
	model.clear();
	if (wpagui == NULL)
		return;

	char reply[20];
	size_t replylen = sizeof(reply) - 1;
	wpagui->ctrlRequest("WPS_ER_START", reply, &replylen);

	add_stations();
	add_scan_results();
}


QStandardItem * Peers::find_addr(QString addr)
{
	if (model.rowCount() == 0)
		return NULL;

	QModelIndexList lst = model.match(model.index(0, 0), peer_role_address,
					  addr);
	if (lst.size() == 0)
		return NULL;
	return model.itemFromIndex(lst[0]);
}


QStandardItem * Peers::find_uuid(QString uuid)
{
	if (model.rowCount() == 0)
		return NULL;

	QModelIndexList lst = model.match(model.index(0, 0), peer_role_uuid,
					  uuid);
	if (lst.size() == 0)
		return NULL;
	return model.itemFromIndex(lst[0]);
}


void Peers::event_notify(WpaMsg msg)
{
	QString text = msg.getMsg();

	if (text.startsWith(WPS_EVENT_PIN_NEEDED)) {
		/*
		 * WPS-PIN-NEEDED 5a02a5fa-9199-5e7c-bc46-e183d3cb32f7
		 * 02:2a:c4:18:5b:f3
		 * [Wireless Client|Company|cmodel|123|12345|1-0050F204-1]
		 */
		QStringList items = text.split(' ');
		QString uuid = items[1];
		QString addr = items[2];
		QString name = "";

		QStandardItem *item = find_addr(addr);
		if (item)
			return;

		int pos = text.indexOf('[');
		if (pos >= 0) {
			int pos2 = text.lastIndexOf(']');
			if (pos2 >= pos) {
				items = text.mid(pos + 1, pos2 - pos - 1).
					split('|');
				name = items[0];
				items.append(addr);
			}
		}

		item = new QStandardItem(*laptop_icon, name);
		if (item) {
			item->setData(addr, peer_role_address);
			item->setData(PEER_TYPE_WPS_PIN_NEEDED,
				      peer_role_type);
			item->setToolTip(items.join(QString("\n")));
			model.appendRow(item);
		}
		return;
	}

	if (text.startsWith(AP_STA_CONNECTED)) {
		/* AP-STA-CONNECTED 02:2a:c4:18:5b:f3 */
		QStringList items = text.split(' ');
		QString addr = items[1];
		QStandardItem *item = find_addr(addr);
		if (item == NULL || item->data(peer_role_type).toInt() !=
		    PEER_TYPE_ASSOCIATED_STATION)
			add_single_station(addr.toAscii().constData());
		return;
	}

	if (text.startsWith(AP_STA_DISCONNECTED)) {
		/* AP-STA-DISCONNECTED 02:2a:c4:18:5b:f3 */
		QStringList items = text.split(' ');
		QString addr = items[1];

		if (model.rowCount() == 0)
			return;

		QModelIndexList lst = model.match(model.index(0, 0),
						  peer_role_address, addr);
		for (int i = 0; i < lst.size(); i++) {
			QStandardItem *item = model.itemFromIndex(lst[i]);
			if (item && item->data(peer_role_type).toInt() ==
			    PEER_TYPE_ASSOCIATED_STATION)
				model.removeRow(lst[i].row());
		}
		return;
	}

	if (text.startsWith(WPS_EVENT_ER_AP_ADD)) {
		/*
		 * WPS-ER-AP-ADD 87654321-9abc-def0-1234-56789abc0002|
		 * Very friendly name|Company|Long description of the model|
		 * WAP|http://w1.fi/|http://w1.fi/hostapd/
		 */
		int pos = text.indexOf(' ');
		if (pos < 0)
			return;
		QStringList items = text.mid(pos + 1).split('|');
		if (items.size() < 2)
			return;

		QStandardItem *item = find_uuid(items[0]);
		if (item)
			return;

		item = new QStandardItem(*ap_icon, items[1]);
		if (item) {
			item->setData(items[0], peer_role_uuid);
			item->setData(PEER_TYPE_WPS_ER_AP, peer_role_type);
			item->setToolTip(items.join(QString("\n")));
			model.appendRow(item);
		}

		return;
	}

	if (text.startsWith(WPS_EVENT_ER_AP_REMOVE)) {
		/* WPS-ER-AP-REMOVE 87654321-9abc-def0-1234-56789abc0002 */
		QStringList items = text.split(' ');
		if (items.size() < 2)
			return;
		if (model.rowCount() == 0)
			return;

		QModelIndexList lst = model.match(model.index(0, 0),
						  peer_role_uuid, items[1]);
		for (int i = 0; i < lst.size(); i++) {
			QStandardItem *item = model.itemFromIndex(lst[i]);
			if (item && item->data(peer_role_type).toInt() ==
			    PEER_TYPE_WPS_ER_AP)
				model.removeRow(lst[i].row());
		}
		return;
	}

	if (text.startsWith(WPS_EVENT_ER_ENROLLEE_ADD)) {
		/*
		 * WPS-ER-ENROLLEE-ADD 2b7093f1-d6fb-5108-adbb-bea66bb87333
		 * 02:66:a0:ee:17:27 M1=1 config_methods=0x14d dev_passwd_id=0
		 * pri_dev_type=1-0050F204-1
		 * |Wireless Client|Company|cmodel|123|12345|
		 */
		QStringList items = text.split(' ');
		if (items.size() < 3)
			return;
		QString uuid = items[1];
		QString addr = items[2];

		int pos = text.indexOf('|');
		if (pos < 0)
			return;
		items = text.mid(pos + 1).split('|');
		if (items.size() < 1)
			return;
		QString name = items[0];
		if (name.length() == 0)
			name = addr;

		remove_enrollee_uuid(uuid);

		QStandardItem *item;
		item = new QStandardItem(*laptop_icon, name);
		if (item) {
			item->setData(uuid, peer_role_uuid);
			item->setData(addr, peer_role_address);
			item->setData(PEER_TYPE_WPS_ER_ENROLLEE,
				      peer_role_type);
			item->setToolTip(items.join(QString("\n")));
			model.appendRow(item);
		}

		return;
	}

	if (text.startsWith(WPS_EVENT_ER_ENROLLEE_REMOVE)) {
		/*
		 * WPS-ER-ENROLLEE-REMOVE 2b7093f1-d6fb-5108-adbb-bea66bb87333
		 * 02:66:a0:ee:17:27
		 */
		QStringList items = text.split(' ');
		if (items.size() < 2)
			return;
		remove_enrollee_uuid(items[1]);
		return;
	}
}


void Peers::closeEvent(QCloseEvent *)
{
	if (wpagui) {
		char reply[20];
		size_t replylen = sizeof(reply) - 1;
		wpagui->ctrlRequest("WPS_ER_STOP", reply, &replylen);
	}
}


void Peers::done(int r)
{
	QDialog::done(r);
	close();
}


void Peers::remove_enrollee_uuid(QString uuid)
{
	if (model.rowCount() == 0)
		return;

	QModelIndexList lst = model.match(model.index(0, 0),
					  peer_role_uuid, uuid);
	for (int i = 0; i < lst.size(); i++) {
		QStandardItem *item = model.itemFromIndex(lst[i]);
		if (item && item->data(peer_role_type).toInt() ==
		    PEER_TYPE_WPS_ER_ENROLLEE)
			model.removeRow(lst[i].row());
	}
}
