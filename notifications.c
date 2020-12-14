/* Copyright (C) 2019 Torge Matthies */
/*
 * This file is part of osu-handler-wine.
 *
 * osu-handler-wine is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * osu-handler-wine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 * Author contact info:
 *   E-Mail address: openglfreak@googlemail.com
 *   GPG key fingerprint: 0535 3830 2F11 C888 9032 FAD2 7C95 CD70 C9E8 438D
 */

#include <gio/gio.h>

void show_notification(char const* const message)
{
    GApplication* application;
    GNotification* notification;
    GIcon* icon;

    application = g_application_new("com.github.openglfreak.osu_handler_wine", G_APPLICATION_FLAGS_NONE);
    g_application_register(application, NULL, NULL);

    notification = g_notification_new("Error");
    g_notification_set_body(notification, message);
    icon = g_themed_icon_new("osu!");
    g_notification_set_icon(notification, icon);
    g_object_unref(icon);

    g_application_send_notification(application, NULL, notification);

    g_object_unref(notification);
    g_object_unref(application);
}
