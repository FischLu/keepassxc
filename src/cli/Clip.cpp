/*
 *  Copyright (C) 2019 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include <cstdlib>
#include <thread>

#include "Clip.h"

#include "cli/TextStream.h"
#include "cli/Utils.h"
#include "core/Database.h"
#include "core/Entry.h"
#include "core/Group.h"

const QCommandLineOption Clip::AttributeOption = QCommandLineOption(
    QStringList() << "a"
                  << "attribute",
    QObject::tr("Copy the given attribute to the clipboard. Defaults to \"password\" if not specified."),
    "attr",
    "password");

const QCommandLineOption Clip::TotpOption =
    QCommandLineOption(QStringList() << "t"
                                     << "totp",
                       QObject::tr("Copy the current TOTP to the clipboard (equivalent to \"-a totp\")."));

Clip::Clip()
{
    name = QString("clip");
    description = QObject::tr("Copy an entry's attribute to the clipboard.");
    options.append(Clip::AttributeOption);
    options.append(Clip::TotpOption);
    positionalArguments.append(
        {QString("entry"), QObject::tr("Path of the entry to clip.", "clip = copy to clipboard"), QString("")});
    optionalArguments.append(
        {QString("timeout"), QObject::tr("Timeout in seconds before clearing the clipboard."), QString("[timeout]")});
}

int Clip::executeWithDatabase(QSharedPointer<Database> database, QSharedPointer<QCommandLineParser> parser)
{
    const QStringList args = parser->positionalArguments();
    const QString& entryPath = args.at(1);
    QString timeout;
    if (args.size() == 3) {
        timeout = args.at(2);
    }
    TextStream errorTextStream(Utils::STDERR);

    int timeoutSeconds = 0;
    if (!timeout.isEmpty() && timeout.toInt() <= 0) {
        errorTextStream << QObject::tr("Invalid timeout value %1.").arg(timeout) << endl;
        return EXIT_FAILURE;
    } else if (!timeout.isEmpty()) {
        timeoutSeconds = timeout.toInt();
    }

    TextStream outputTextStream(parser->isSet(Command::QuietOption) ? Utils::DEVNULL : Utils::STDOUT,
                                QIODevice::WriteOnly);
    Entry* entry = database->rootGroup()->findEntryByPath(entryPath);
    if (!entry) {
        errorTextStream << QObject::tr("Entry %1 not found.").arg(entryPath) << endl;
        return EXIT_FAILURE;
    }

    if (parser->isSet(AttributeOption) && parser->isSet(TotpOption)) {
        errorTextStream << QObject::tr("ERROR: Please specify one of --attribute or --totp, not both.") << endl;
        return EXIT_FAILURE;
    }

    QString selectedAttribute = parser->value(AttributeOption);
    QString value;
    bool found = false;
    if (parser->isSet(TotpOption) || selectedAttribute == "totp") {
        if (!entry->hasTotp()) {
            errorTextStream << QObject::tr("Entry with path %1 has no TOTP set up.").arg(entryPath) << endl;
            return EXIT_FAILURE;
        }

        found = true;
        value = entry->totp();
    } else {
        QStringList attrs = Utils::findAttributes(*entry->attributes(), selectedAttribute);
        if (attrs.size() > 1) {
            errorTextStream << QObject::tr("ERROR: attribute %1 is ambiguous, it matches %2.")
                                   .arg(selectedAttribute, QLocale().createSeparatedList(attrs))
                            << endl;
            return EXIT_FAILURE;
        } else if (attrs.size() == 1) {
            found = true;
            selectedAttribute = attrs[0];
            value = entry->attributes()->value(selectedAttribute);
        }
    }

    if (!found) {
        outputTextStream << QObject::tr("Attribute \"%1\" not found.").arg(selectedAttribute) << endl;
        return EXIT_FAILURE;
    }

    int exitCode = Utils::clipText(value);
    if (exitCode != EXIT_SUCCESS) {
        return exitCode;
    }

    outputTextStream << QObject::tr("Entry's \"%1\" attribute copied to the clipboard!").arg(selectedAttribute) << endl;

    if (!timeoutSeconds) {
        return exitCode;
    }

    QString lastLine = "";
    while (timeoutSeconds > 0) {
        outputTextStream << '\r' << QString(lastLine.size(), ' ') << '\r';
        lastLine = QObject::tr("Clearing the clipboard in %1 second(s)...", "", timeoutSeconds).arg(timeoutSeconds);
        outputTextStream << lastLine << flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        --timeoutSeconds;
    }
    Utils::clipText("");
    outputTextStream << '\r' << QString(lastLine.size(), ' ') << '\r';
    outputTextStream << QObject::tr("Clipboard cleared!") << endl;

    return EXIT_SUCCESS;
}
