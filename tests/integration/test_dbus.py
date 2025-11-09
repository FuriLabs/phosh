#!/usr/bin/env python3

# Copyright (C) 2025 Phosh.mobi e.V.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Inspired by g-c-c's scenario tester which is
# Copyright (C) 2021 Red Hat Inc.
# Author: Bastien Nocera <hadess@hadess.net>
#
# Author: Guido Günther <agx@sigxcpu.org>

import os
import subprocess
import dbusmock
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbus.mainloop.glib import DBusGMainLoop

from gi.repository import Gio

from . import Phosh, set_nonblock

DBusGMainLoop(set_as_default=True)


class PhoshDBusTestCase(DBusTestCase):
    @classmethod
    def setUpClass(klass):
        klass.mocks = OrderedDict()
        env = {}

        # Start system bus
        DBusTestCase.setUpClass()
        klass.test_bus = Gio.TestDBus.new(Gio.TestDBusFlags.NONE)
        klass.test_bus.up()
        os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = klass.test_bus.get_bus_address()

        # Start session bus
        klass.session_test_bus = Gio.TestDBus.new(Gio.TestDBusFlags.NONE)
        klass.session_test_bus.up()
        os.environ["DBUS_SESSION_BUS_ADDRESS"] = (
            klass.session_test_bus.get_bus_address()
        )

        # Add the templates we want running before phosh starts
        # TODO: start udev mock for e.g. backlight
        klass.start_from_template("bluez5")
        klass.start_from_template("gsd_rfkill")
        klass.start_from_template("modemmanager")
        klass.start_from_template("networkmanager")

        # Setup logging
        env["G_MESSAGES_DEBUG"] = " ".join(
            [
                "phosh-bt-manager",
                "phosh-cell-broadcast-manager",
                "phosh-wifi-manager",
                "phosh-wwan-manager",
                "phosh-wwan-mm",
            ]
        )
        env["XDG_CURRENT_DESKTOP"] = "Phosh:GNOME"

        topsrcdir = os.environ["TOPSRCDIR"]
        topbuilddir = os.environ["TOPBUILDDIR"]
        klass.phosh = Phosh(topsrcdir, topbuilddir, env).spawn_nested()

    @classmethod
    def tearDownClass(klass):
        success = klass.phosh.teardown_nested()
        assert success is True

        for mock_server, mock_obj in reversed(klass.mocks.values()):
            mock_server.terminate()
            mock_server.wait()

        DBusTestCase.tearDownClass()

        criticals = klass.phosh.get_criticals()
        assert not criticals, f"Log contains criticals: {criticals}"

        warnings = klass.phosh.get_warnings()
        if warnings:
            print(f"Log contains warnings: {warnings}")

    @classmethod
    def start_from_template(klass, template, params={}):
        mock_server, mock_obj = klass.spawn_server_template(
            template, params, stdout=subprocess.PIPE
        )
        set_nonblock(mock_server.stdout)
        mocks = (mock_server, mock_obj)
        assert klass.mocks.setdefault(template, mocks) == mocks
        return mocks

    def __init__(self, methodName):
        super().__init__(methodName)

    def test_mm(self):
        mm = self.mocks["modemmanager"][1]
        mm.AddSimpleModem()

        assert self.phosh.wait_for_output(" Modem is present\n")
        assert self.phosh.check_for_stdout(" Enabling cell broadcast interface")

        # No data connection yet:
        assert self.phosh.check_for_stdout(" WWAN data connection present: 0")
        subprocess.check_output(["nmcli", "c", "add", "connection.type", "gsm"])
        # Data connection available:
        assert self.phosh.wait_for_output(" WWAN data connection present: 1")

        # Add cell broadcast message
        cbm_text = "Dies ist ein Test für Cellbroadcasts"
        cbm_channel = 4371
        mm.AddCbm(2, cbm_channel, cbm_text)
        assert self.phosh.wait_for_output(f" Received cbm {cbm_channel}: {cbm_text}")

    def test_wifi(self):
        nm = self.mocks["networkmanager"][1]

        assert self.phosh.check_for_stdout(" NM Wi-Fi enabled: 0, present: 0")

        # Add and enable Wi-Fi
        nm.AddWiFiDevice(
            "wifi0", "wlan0", dbusmock.templates.networkmanager.DeviceState.ACTIVATED
        )
        assert self.phosh.wait_for_output(" NM Wi-Fi enabled: 1, present: 1")

    def test_bt(self):
        adapter_name = "hci0"

        assert self.phosh.wait_for_output(" BT present: 1", ignore_present=True)
        assert self.phosh.check_for_stdout(" BT enabled: 1")

        bt = self.mocks["bluez5"][1]
        bt.AddAdapter(adapter_name, "my-phone")
        assert self.phosh.wait_for_output(" State: BLUETOOTH_ADAPTER_STATE_ON")
