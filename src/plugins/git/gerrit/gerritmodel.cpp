// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritmodel.h"
#include "gerritparameters.h"
#include "../gitclient.h"
#include "../gittr.h"

#include <coreplugin/progressmanager/processprogress.h>
#include <vcsbase/vcsoutputwindow.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/qtcprocess.h>
#include <utils/processinterface.h>

#include <QApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVariant>

enum { debug = 0 };

using namespace Utils;
using namespace VcsBase;

namespace Gerrit::Internal {

QDebug operator<<(QDebug d, const GerritApproval &a)
{
    d.nospace() << a.reviewer.fullName << ": " << a.approval << " ("
                << a.type << ", " << a.description << ')';
    return d;
}

// Sort approvals by type and reviewer
bool gerritApprovalLessThan(const GerritApproval &a1, const GerritApproval &a2)
{
    const int compare = a1.type.compare(a2.type);
    return compare == 0 ? a1.reviewer.fullName.compare(a2.reviewer.fullName) < 0 : compare < 0;
}

QDebug operator<<(QDebug d, const GerritPatchSet &p)
{
    d.nospace() << " Patch set: " << p.ref << ' ' << p.patchSetNumber
                << ' ' << p.approvals;
    return d;
}

QDebug operator<<(QDebug d, const GerritChange &c)
{
    d.nospace() << c.fullTitle() << " by " << c.owner.email
                << ' ' << c.lastUpdated << ' ' <<  c.currentPatchSet;
    return d;
}

// Format default Url for a change
static QString defaultUrl(const GerritServer &server, int gerritNumber)
{
    QString result = QLatin1String(gerritSettings().https ? "https://" : "http://");
    result += server.host;
    result += '/';
    result += QString::number(gerritNumber);
    return result;
}

// Format (sorted) approvals as separate HTML table
// lines by type listing the revievers:
// "<tr><td>Code Review</td><td>John Doe: -1, ...</tr><tr>...Sanity Review: ...".
QString GerritPatchSet::approvalsToHtml() const
{
    if (approvals.isEmpty())
        return {};

    QString result;
    QTextStream str(&result);
    QString lastType;
    for (const GerritApproval &a : approvals) {
        if (a.type != lastType) {
            if (!lastType.isEmpty())
                str << "</tr>\n";
            str << "<tr><td>"
                << (a.description.isEmpty() ? a.type : a.description)
                << "</td><td>";
            lastType = a.type;
        } else {
            str << ", ";
        }
        str << a.reviewer.fullName;
        if (!a.reviewer.email.isEmpty())
            str << " <a href=\"mailto:" << a.reviewer.email << "\">" << a.reviewer.email << "</a>";
        str << ": ";
        if (a.approval >= 0)
            str << '+';
        str << a.approval;
    }
    str << "</tr>\n";
    return result;
}

// Determine total approval level. Negative values take preference
// and stay.
static inline void applyApproval(int approval, int *total)
{
    if (approval < *total || (*total >= 0 && approval > *total))
        *total = approval;
}

// Format the approvals similar to the columns in the Web view
// by a type character followed by the approval level: "C: -2, S: 1"
QString GerritPatchSet::approvalsColumn() const
{
    using TypeReviewMap = QMap<QChar, int>;
    using TypeReviewMapIterator = TypeReviewMap::iterator;
    using TypeReviewMapConstIterator = TypeReviewMap::const_iterator;

    QString result;
    if (approvals.isEmpty())
        return result;

    TypeReviewMap reviews; // Sort approvals into a map by type character
    for (const GerritApproval &a : approvals) {
        if (a.type != "STGN") { // Qt-Project specific: Ignore "STGN" (Staged)
            const QChar typeChar = a.type.at(0);
            TypeReviewMapIterator it = reviews.find(typeChar);
            if (it == reviews.end())
                it = reviews.insert(typeChar, 0);
            applyApproval(a.approval, &it.value());
        }
    }

    QTextStream str(&result);
    const TypeReviewMapConstIterator cend = reviews.constEnd();
    for (TypeReviewMapConstIterator it = reviews.constBegin(); it != cend; ++it) {
        if (!result.isEmpty())
            str << ' ';
        str << it.key() << ": ";
        if (it.value() >= 0)
            str << '+';
        str << it.value();
    }
    return result;
}

bool GerritPatchSet::hasApproval(const GerritUser &user) const
{
    return Utils::contains(approvals, [&user](const GerritApproval &a) {
        return a.reviewer.isSameAs(user);
    });
}

int GerritPatchSet::approvalLevel() const
{
    int value = 0;
    for (const GerritApproval &a : approvals)
        applyApproval(a.approval, &value);
    return value;
}

QString GerritChange::filterString() const
{
    const QChar blank = ' ';
    QString result = QString::number(number) + blank + title + blank
            + owner.fullName + blank + project + blank
            + branch + blank + status;
    for (const GerritApproval &a : currentPatchSet.approvals) {
        result += blank;
        result += a.reviewer.fullName;
    }
    return result;
}

QStringList GerritChange::gitFetchArguments(const GerritServer &server) const
{
    const QString url = currentPatchSet.url.isEmpty()
            ? server.url(GerritServer::UrlWithHttpUser) + '/' + project
            : currentPatchSet.url;
    return {"fetch", url, currentPatchSet.ref};
}

QString GerritChange::fullTitle() const
{
    QString res = title;
    if (status == "DRAFT")
        res += Git::Tr::tr(" (Draft)");
    return res;
}

// Helper class that runs ssh gerrit queries from a list of query argument
// string lists,
// see http://gerrit.googlecode.com/svn/documentation/2.1.5/cmd-query.html
// In theory, querying uses a continuation/limit protocol, but we assume
// we will never reach a limit with those queries.

class QueryContext : public QObject
{
    Q_OBJECT
public:
    QueryContext(const QString &query,
                 const GerritServer &server,
                 QObject *parent = nullptr);

    ~QueryContext() override;
    void start();
    void terminate();

signals:
    void resultRetrieved(const QByteArray &);
    void errorText(const QString &text);
    void finished();

private:
    void processDone();
    void timeout();

    Process m_process;
    QTimer m_timer;
    FilePath m_binary;
    QByteArray m_output;
    QString m_error;
    QStringList m_arguments;
};

enum { timeOutMS = 30000 };

QueryContext::QueryContext(const QString &query,
                           const GerritServer &server,
                           QObject *parent)
    : QObject(parent)
{
    m_process.setUseCtrlCStub(true);
    if (server.type == GerritServer::Ssh) {
        m_binary = gerritSettings().ssh;
        if (server.port)
            m_arguments << gerritSettings().portFlag << QString::number(server.port);
        m_arguments << server.hostArgument() << "gerrit"
                    << "query" << "--dependencies"
                    << "--current-patch-set"
                    << "--format=JSON" << query;
    } else {
        m_binary = gerritSettings().curl;
        const QString url = server.url(GerritServer::RestUrl) + "/changes/?q="
                + QString::fromUtf8(QUrl::toPercentEncoding(query))
                + "&o=CURRENT_REVISION&o=DETAILED_LABELS&o=DETAILED_ACCOUNTS";
        m_arguments = server.curlArguments() << url;
    }
    connect(&m_process, &Process::readyReadStandardError, this, [this] {
        const QString text = QString::fromLocal8Bit(m_process.readAllRawStandardError());
        VcsOutputWindow::appendError(m_process.workingDirectory(), text);
        m_error.append(text);
    });
    connect(&m_process, &Process::readyReadStandardOutput, this, [this] {
        m_output.append(m_process.readAllRawStandardOutput());
    });
    connect(&m_process, &Process::done, this, &QueryContext::processDone);

    m_timer.setInterval(timeOutMS);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QueryContext::timeout);
}

QueryContext::~QueryContext()
{
    if (m_timer.isActive())
        m_timer.stop();
}

void QueryContext::start()
{
    // Order: synchronous call to error handling if something goes wrong.
    const CommandLine commandLine{m_binary, m_arguments};
    VcsOutputWindow::appendCommand(m_process.workingDirectory(), commandLine);
    m_timer.start();
    m_process.setCommand(commandLine);
    m_process.setEnvironment(Git::Internal::gitClient().processEnvironment(m_binary));
    auto progress = new Core::ProcessProgress(&m_process);
    progress->setDisplayName(Git::Tr::tr("Querying Gerrit"));
    m_process.start();
}

void QueryContext::terminate()
{
    m_process.stop();
    m_process.waitForFinished();
}

void QueryContext::processDone()
{
    if (m_timer.isActive())
        m_timer.stop();

    if (!m_error.isEmpty())
        emit errorText(m_error);

    if (m_process.result() == ProcessResult::FinishedWithSuccess)
        emit resultRetrieved(m_output);
    else if (m_process.result() != ProcessResult::Canceled)
        VcsOutputWindow::appendError(m_process.workingDirectory(), m_process.exitMessage());

    emit finished();
}

void QueryContext::timeout()
{
    if (m_process.state() != QProcess::Running)
        return;

    QWidget *parent = QApplication::activeModalWidget();
    if (!parent)
        parent = QApplication::activeWindow();
    QMessageBox box(QMessageBox::Question, Git::Tr::tr("Timeout"),
                    Git::Tr::tr("The gerrit process has not responded within %1 s.\n"
                       "Most likely this is caused by problems with SSH authentication.\n"
                       "Would you like to terminate it?").
                    arg(timeOutMS / 1000), QMessageBox::NoButton, parent);
    QPushButton *terminateButton = box.addButton(Git::Tr::tr("Terminate"), QMessageBox::YesRole);
    box.addButton(Git::Tr::tr("Keep Running"), QMessageBox::NoRole);
    connect(&m_process, &Process::done, &box, &QDialog::reject);
    box.exec();
    if (m_process.state() != QProcess::Running)
        return;
    if (box.clickedButton() == terminateButton)
        terminate();
    else
        m_timer.start();
}

GerritModel::GerritModel(QObject *parent)
    : QStandardItemModel(0, ColumnCount, parent)
{
    QStringList headers; // Keep in sync with GerritChange::toHtml()
    headers << "#" << Git::Tr::tr("Subject") << Git::Tr::tr("Owner")
            << Git::Tr::tr("Updated") << Git::Tr::tr("Project")
            << Git::Tr::tr("Approvals") << Git::Tr::tr("Status");
    setHorizontalHeaderLabels(headers);
}

GerritModel::~GerritModel() = default;

QVariant GerritModel::data(const QModelIndex &index, int role) const
{
    QVariant value = QStandardItemModel::data(index, role);
    if (role == SortRole && value.isNull())
        return QStandardItemModel::data(index, Qt::DisplayRole);
    return value;
}

static inline GerritChangePtr changeFromItem(const QStandardItem *item)
{
    return qvariant_cast<GerritChangePtr>(item->data(GerritModel::GerritChangeRole));
}

GerritChangePtr GerritModel::change(const QModelIndex &index) const
{
    if (index.isValid())
        return changeFromItem(itemFromIndex(index));
    return GerritChangePtr(new GerritChange);
}

QString GerritModel::dependencyHtml(const QString &header, const int changeNumber,
                                    const QString &serverPrefix) const
{
    QString res;
    if (!changeNumber)
        return res;
    QTextStream str(&res);
    str << "<tr><td>" << header << "</td><td><a href="
        << serverPrefix << "r/" << changeNumber << '>' << changeNumber << "</a>";
    if (const QStandardItem *item = itemForNumber(changeNumber))
        str << " (" << changeFromItem(item)->fullTitle() << ')';
    str << "</td></tr>";
    return res;
}

QString GerritModel::toHtml(const QModelIndex& index) const
{
    static const QString subjectHeader = Git::Tr::tr("Subject");
    static const QString numberHeader = Git::Tr::tr("Number");
    static const QString ownerHeader = Git::Tr::tr("Owner");
    static const QString projectHeader = Git::Tr::tr("Project");
    static const QString statusHeader = Git::Tr::tr("Status");
    static const QString patchSetHeader = Git::Tr::tr("Patch set");
    static const QString urlHeader = Git::Tr::tr("URL");
    static const QString dependsOnHeader = Git::Tr::tr("Depends on");
    static const QString neededByHeader = Git::Tr::tr("Needed by");

    if (!index.isValid())
        return {};
    const GerritChangePtr c = change(index);
    const QString serverPrefix = c->url.left(c->url.lastIndexOf('/') + 1);
    QString result;
    QTextStream str(&result);
    str << "<html><head/><body><table>"
        << "<tr><td>" << subjectHeader << "</td><td>" << c->fullTitle() << "</td></tr>"
        << "<tr><td>" << numberHeader << "</td><td><a href=\"" << c->url << "\">" << c->number << "</a></td></tr>"
        << "<tr><td>" << ownerHeader << "</td><td>" << c->owner.fullName << ' '
        << "<a href=\"mailto:" << c->owner.email << "\">" << c->owner.email << "</a></td></tr>"
        << "<tr><td>" << projectHeader << "</td><td>" << c->project << " (" << c->branch << ")</td></tr>"
        << dependencyHtml(dependsOnHeader, c->dependsOnNumber, serverPrefix)
        << dependencyHtml(neededByHeader, c->neededByNumber, serverPrefix)
        << "<tr><td>" << statusHeader << "</td><td>" << c->status
        << ", " << QLocale::system().toString(c->lastUpdated, QLocale::ShortFormat) << "</td></tr>"
        << "<tr><td>" << patchSetHeader << "</td><td>" << "</td></tr>" << c->currentPatchSet.patchSetNumber << "</td></tr>"
        << c->currentPatchSet.approvalsToHtml()
        << "<tr><td>" << urlHeader << "</td><td><a href=\"" << c->url << "\">" << c->url << "</a></td></tr>"
        << "</table></body></html>";
    return result;
}

static QStandardItem *numberSearchRecursion(QStandardItem *item, int number)
{
    if (changeFromItem(item)->number == number)
        return item;
    const int rowCount = item->rowCount();
    for (int r = 0; r < rowCount; ++r) {
        if (QStandardItem *i = numberSearchRecursion(item->child(r, 0), number))
            return i;
    }
    return nullptr;
}

QStandardItem *GerritModel::itemForNumber(int number) const
{
    if (!number)
        return nullptr;
    const int numRows = rowCount();
    for (int r = 0; r < numRows; ++r) {
        if (QStandardItem *i = numberSearchRecursion(item(r, 0), number))
            return i;
    }
    return nullptr;
}

void GerritModel::refresh(const std::shared_ptr<GerritServer> &server, const QString &query)
{
    if (m_query)
        m_query->terminate();
    clearData();
    m_server = server;

    QString realQuery = query.trimmed();
    if (realQuery.isEmpty()) {
        realQuery = "status:open";
        const QString user = m_server->user.userName;
        if (!user.isEmpty())
            realQuery += QString(" (owner:%1 OR reviewer:%1)").arg(user);
    }

    m_query = new QueryContext(realQuery, *m_server, this);
    connect(m_query, &QueryContext::resultRetrieved, this, &GerritModel::resultRetrieved);
    connect(m_query, &QueryContext::errorText, this, &GerritModel::errorText);
    connect(m_query, &QueryContext::finished, this, &GerritModel::queryFinished);
    emit refreshStateChanged(true);
    m_query->start();
    setState(Running);
}

void GerritModel::clearData()
{
    if (const int rows = rowCount())
        removeRows(0, rows);
}

void GerritModel::setState(GerritModel::QueryState s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
}

// {"name":"Hans Mustermann","email":"hm@acme.com","username":"hansm"}
static GerritUser parseGerritUser(const QJsonObject &object)
{
    GerritUser user;
    user.userName = object.value("username").toString();
    user.fullName = object.value("name").toString();
    user.email = object.value("email").toString();
    return user;
}

static int numberValue(const QJsonObject &object)
{
    const QJsonValue number = object.value("number");
    // Since Gerrit 2.14 (commits fa92467dc and b0cfe1401) the change and patch set numbers are int
    return number.isString() ? number.toString().toInt() : number.toInt();
}

/* Parse gerrit query Json output.
 * See http://gerrit.googlecode.com/svn/documentation/2.1.5/cmd-query.html
 * Note: The url will be present only if  "canonicalWebUrl" is configured
 * in gerrit.config.
\code
{"project":"qt/qtbase","branch":"master","id":"I6601ca68c427b909680423ae81802f1ed5cd178a",
"number":"24143","subject":"bla","owner":{"name":"Hans Mustermann","email":"hm@acme.com"},
"url":"https://...","lastUpdated":1335127888,"sortKey":"001c8fc300005e4f",
"open":true,"status":"NEW","currentPatchSet":
  {"number":"1","revision":"0a1e40c78ef16f7652472f4b4bb4c0addeafbf82",
   "ref":"refs/changes/43/24143/1",
   "uploader":{"name":"Hans Mustermann","email":"hm@acme.com"},
   "approvals":[{"type":"SRVW","description":"Sanity Review","value":"1",
                 "grantedOn":1335127888,"by":{
                 "name":"Qt Sanity Bot","email":"qt_sanity_bot@ovi.com"}}]}}
\endcode
*/

static GerritChangePtr parseSshOutput(const QJsonObject &object)
{
    GerritChangePtr change(new GerritChange);
    // Read current patch set.
    const QJsonObject patchSet = object.value("currentPatchSet").toObject();
    change->currentPatchSet.patchSetNumber = qMax(1, numberValue(patchSet));
    change->currentPatchSet.ref = patchSet.value("ref").toString();
    const QJsonArray approvalsJ = patchSet.value("approvals").toArray();
    const int ac = approvalsJ.size();
    for (int a = 0; a < ac; ++a) {
        const QJsonObject ao = approvalsJ.at(a).toObject();
        GerritApproval approval;
        approval.reviewer = parseGerritUser(ao.value("by").toObject());
        approval.approval = ao.value("value").toString().toInt();
        approval.type = ao.value("type").toString();
        approval.description = ao.value("description").toString();
        change->currentPatchSet.approvals.push_back(approval);
    }
    std::stable_sort(change->currentPatchSet.approvals.begin(),
                     change->currentPatchSet.approvals.end(),
                     gerritApprovalLessThan);
    // Remaining
    change->number = numberValue(object);
    change->url = object.value("url").toString();
    change->title = object.value("subject").toString();
    change->owner = parseGerritUser(object.value("owner").toObject());
    change->project = object.value("project").toString();
    change->branch = object.value("branch").toString();
    change->status =  object.value("status").toString();
    if (const int timeT = object.value("lastUpdated").toInt())
        change->lastUpdated = QDateTime::fromSecsSinceEpoch(timeT);
    // Read out dependencies
    const QJsonValue dependsOnValue = object.value("dependsOn");
    if (dependsOnValue.isArray()) {
        const QJsonArray dependsOnArray = dependsOnValue.toArray();
        if (!dependsOnArray.isEmpty()) {
            const QJsonValue first = dependsOnArray.at(0);
            if (first.isObject())
                change->dependsOnNumber = numberValue(first.toObject());
        }
    }
    // Read out needed by
    const QJsonValue neededByValue = object.value("neededBy");
    if (neededByValue.isArray()) {
        const QJsonArray neededByArray = neededByValue.toArray();
        if (!neededByArray.isEmpty()) {
            const QJsonValue first = neededByArray.at(0);
            if (first.isObject())
                change->neededByNumber = numberValue(first.toObject());
        }
    }
    return change;
}

/*
  {
    "kind": "gerritcodereview#change",
    "id": "qt-creator%2Fqt-creator~master~Icc164b9d84abe4efc34deaa5d19dca167fdb14e1",
    "project": "qt-creator/qt-creator",
    "branch": "master",
    "change_id": "Icc164b9d84abe4efc34deaa5d19dca167fdb14e1",
    "subject": "WIP: Gerrit: Support REST query for HTTP servers",
    "status": "NEW",
    "created": "2017-02-22 21:23:39.403000000",
    "updated": "2017-02-23 21:03:51.055000000",
    "reviewed": true,
    "mergeable": false,
    "_sortkey": "004368cf0002d84f",
    "_number": 186447,
    "owner": {
      "_account_id": 1000534,
      "name": "Orgad Shaneh",
      "email": "orgads@gmail.com"
    },
    "labels": {
      "Code-Review": {
        "all": [
          {
            "value": 0,
            "_account_id": 1000009,
            "name": "Tobias Hunger",
            "email": "tobias.hunger@qt.io"
          },
          {
            "value": 0,
            "_account_id": 1000528,
            "name": "Andre Hartmann",
            "email": "aha_1980@gmx.de"
          },
          {
            "value": 0,
            "_account_id": 1000049,
            "name": "Qt Sanity Bot",
            "email": "qt_sanitybot@qt-project.org"
          }
        ],
        "values": {
          "-2": "This shall not be merged",
          "-1": "I would prefer this is not merged as is",
          " 0": "No score",
          "+1": "Looks good to me, but someone else must approve",
          "+2": "Looks good to me, approved"
        }
      },
      "Sanity-Review": {
        "all": [
          {
            "value": 0,
            "_account_id": 1000009,
            "name": "Tobias Hunger",
            "email": "tobias.hunger@qt.io"
          },
          {
            "value": 0,
            "_account_id": 1000528,
            "name": "Andre Hartmann",
            "email": "aha_1980@gmx.de"
          },
          {
            "value": 1,
            "_account_id": 1000049,
            "name": "Qt Sanity Bot",
            "email": "qt_sanitybot@qt-project.org"
          }
        ],
        "values": {
          "-2": "Major sanity problems found",
          "-1": "Sanity problems found",
          " 0": "No sanity review",
          "+1": "Sanity review passed"
        }
      }
    },
    "permitted_labels": {
      "Code-Review": [
        "-2",
        "-1",
        " 0",
        "+1",
        "+2"
      ],
      "Sanity-Review": [
        "-2",
        "-1",
        " 0",
        "+1"
      ]
    },
    "current_revision": "87916545e2974913d56f56c9f06fc3822a876aca",
    "revisions": {
      "87916545e2974913d56f56c9f06fc3822a876aca": {
        "draft": true,
        "_number": 2,
        "fetch": {
          "http": {
            "url": "https://codereview.qt-project.org/qt-creator/qt-creator",
            "ref": "refs/changes/47/186447/2"
          },
          "ssh": {
            "url": "ssh:// *:29418/qt-creator/qt-creator",
            "ref": "refs/changes/47/186447/2"
          }
        }
      }
    }
  }
*/

static int restNumberValue(const QJsonObject &object)
{
    return object.value("_number").toInt();
}

static GerritChangePtr parseRestOutput(const QJsonObject &object, const GerritServer &server)
{
    GerritChangePtr change(new GerritChange);
    change->number = restNumberValue(object);
    change->url = QString("%1/%2").arg(server.url()).arg(change->number);
    change->title = object.value("subject").toString();
    change->owner = parseGerritUser(object.value("owner").toObject());
    change->project = object.value("project").toString();
    change->branch = object.value("branch").toString();
    change->status =  object.value("status").toString();
    change->lastUpdated = QDateTime::fromString(object.value("updated").toString() + "Z",
                                                Qt::DateFormat::ISODate).toLocalTime();
    // Read current patch set.
    const QJsonObject patchSet = object.value("revisions").toObject().begin().value().toObject();
    change->currentPatchSet.patchSetNumber = qMax(1, restNumberValue(patchSet));
    const QJsonObject fetchInfo = patchSet.value("fetch").toObject().value("http").toObject();
    change->currentPatchSet.ref = fetchInfo.value("ref").toString();
    // Replace * in ssh://*:29418/qt-creator/qt-creator with the hostname
    change->currentPatchSet.url = fetchInfo.value("url").toString().replace('*', server.host);
    const QJsonObject labels = object.value("labels").toObject();
    for (auto it = labels.constBegin(), end = labels.constEnd(); it != end; ++it) {
        const QJsonArray all = it.value().toObject().value("all").toArray();
        for (const QJsonValue &av : all) {
            const QJsonObject ao = av.toObject();
            const int value = ao.value("value").toInt();
            if (!value)
                continue;
            GerritApproval approval;
            approval.reviewer = parseGerritUser(ao);
            approval.approval = value;
            approval.type = it.key();
            change->currentPatchSet.approvals.push_back(approval);
        }
    }
    std::stable_sort(change->currentPatchSet.approvals.begin(),
                     change->currentPatchSet.approvals.end(),
                     gerritApprovalLessThan);
    return change;
}

static bool parseOutput(const GerritServer &server,
                        const QByteArray &output,
                        QList<GerritChangePtr> &result)
{
    QByteArray adaptedOutput;
    if (server.type == GerritServer::Ssh) {
        // The output consists of separate lines containing a document each
        // Add a comma after each line (except the last), and enclose it as an array
        adaptedOutput = '[' + output + ']';
        adaptedOutput.replace('\n', ',');
        const int lastComma = adaptedOutput.lastIndexOf(',');
        if (lastComma >= 0)
            adaptedOutput[lastComma] = '\n';
    } else {
        adaptedOutput = output;
        // Strip first line, which is )]}'
        if (adaptedOutput.startsWith(')'))
            adaptedOutput.remove(0, adaptedOutput.indexOf("\n"));
    }
    bool res = true;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(adaptedOutput, &error);
    if (doc.isNull()) {
        const QString errorMessage = Git::Tr::tr("Parse error: \"%1\" -> %2")
                                         .arg(QString::fromUtf8(output), error.errorString());
        qWarning() << errorMessage;
        VcsOutputWindow::appendError({}, errorMessage);
        res = false;
    }
    const QJsonArray rootArray = doc.array();
    result.clear();
    result.reserve(rootArray.count());
    for (const QJsonValue &value : rootArray) {
        const QJsonObject object = value.toObject();
        // Skip stats line: {"type":"stats","rowCount":9,"runTimeMilliseconds":13}
        if (object.contains("type"))
            continue;
        GerritChangePtr change =
                (server.type == GerritServer::Ssh ? parseSshOutput(object)
                                                  : parseRestOutput(object, server));
        if (change->isValid()) {
            if (change->url.isEmpty()) //  No "canonicalWebUrl" is in gerrit.config.
                change->url = defaultUrl(server, change->number);
            result.push_back(change);
        } else {
            const QByteArray jsonObject = QJsonDocument(object).toJson();
            qWarning("%s: Parse error: '%s'.", Q_FUNC_INFO, jsonObject.constData());
            VcsOutputWindow::appendError({}, Git::Tr::tr("Parse error: \"%1\"")
                                  .arg(QString::fromUtf8(jsonObject)));
            res = false;
        }
    }
    return res;
}

QList<QStandardItem *> GerritModel::changeToRow(const GerritChangePtr &c) const
{
    QList<QStandardItem *> row;
    const QVariant filterV = QVariant(c->filterString());
    const QVariant changeV = QVariant::fromValue(c);
    for (int i = 0; i < GerritModel::ColumnCount; ++i) {
        auto item = new QStandardItem;
        item->setData(changeV, GerritModel::GerritChangeRole);
        item->setData(filterV, GerritModel::FilterRole);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        row.append(item);
    }
    row[NumberColumn]->setData(c->number, Qt::DisplayRole);
    row[TitleColumn]->setText(c->fullTitle());
    row[OwnerColumn]->setText(c->owner.fullName);
    // Shorten columns: Display time if it is today, else date
    const QString dateString = c->lastUpdated.date() == QDate::currentDate() ?
                QLocale::system().toString(c->lastUpdated.time(), QLocale::ShortFormat) :
                QLocale::system().toString(c->lastUpdated.date(), QLocale::ShortFormat);
    row[DateColumn]->setData(dateString, Qt::DisplayRole);
    row[DateColumn]->setData(c->lastUpdated, SortRole);

    QString project = c->project;
    if (c->branch != "master")
        project += " (" + c->branch  + ')';
    row[ProjectColumn]->setText(project);
    row[StatusColumn]->setText(c->status);
    row[ApprovalsColumn]->setText(c->currentPatchSet.approvalsColumn());
    // Mark changes awaiting action using a bold font.
    bool bold = false;
    if (c->owner.isSameAs(m_server->user)) { // Owned changes: Review != 0,1. Submit or amend.
        const int level = c->currentPatchSet.approvalLevel();
        bold = level != 0 && level != 1;
    } else { // Changes pending for review: No review yet.
        bold = !c->currentPatchSet.hasApproval(m_server->user);
    }
    if (bold) {
        QFont font = row.first()->font();
        font.setBold(true);
        for (int i = 0; i < GerritModel::ColumnCount; ++i)
            row[i]->setFont(font);
    }

    return row;
}

bool gerritChangeLessThan(const GerritChangePtr &c1, const GerritChangePtr &c2)
{
    if (c1->depth != c2->depth)
        return c1->depth < c2->depth;
    return c1->lastUpdated > c2->lastUpdated;
}

void GerritModel::resultRetrieved(const QByteArray &output)
{
    QList<GerritChangePtr> changes;
    setState(parseOutput(*m_server, output, changes) ? Ok : Error);

    // Populate a hash with indices for faster access.
    QHash<int, int> numberIndexHash;
    const int count = changes.size();
    for (int i = 0; i < count; ++i)
        numberIndexHash.insert(changes.at(i)->number, i);
    // Mark root nodes: Changes that do not have a dependency, depend on a change
    // not in the list or on a change that is not "NEW".
    for (int i = 0; i < count; ++i) {
        if (!changes.at(i)->dependsOnNumber) {
            changes.at(i)->depth = 0;
        } else {
            const int dependsOnIndex = numberIndexHash.value(changes.at(i)->dependsOnNumber, -1);
            if (dependsOnIndex < 0 || changes.at(dependsOnIndex)->status != "NEW")
                changes.at(i)->depth = 0;
        }
    }
    // Indicate depth of dependent changes by using that of the parent + 1 until no more
    // changes occur.
    for (bool changed = true; changed; ) {
        changed = false;
        for (int i = 0; i < count; ++i) {
            if (changes.at(i)->depth < 0) {
                const int dependsIndex = numberIndexHash.value(changes.at(i)->dependsOnNumber);
                const int dependsOnDepth = changes.at(dependsIndex)->depth;
                if (dependsOnDepth >= 0) {
                    changes.at(i)->depth = dependsOnDepth + 1;
                    changed = true;
                }
            }
        }
    }
    // Sort by depth (root nodes first) and by date.
    std::stable_sort(changes.begin(), changes.end(), gerritChangeLessThan);
    numberIndexHash.clear();

    for (const GerritChangePtr &c : std::as_const(changes)) {
        // Avoid duplicate entries for example in the (unlikely)
        // case people do self-reviews.
        if (!itemForNumber(c->number)) {
            const QList<QStandardItem *> newRow = changeToRow(c);
            if (c->depth) {
                QStandardItem *parent = itemForNumber(c->dependsOnNumber);
                // Append changes with depth > 1 to the parent with depth=1 to avoid
                // too-deeply nested items.
                for (; changeFromItem(parent)->depth >= 1; parent = parent->parent()) {}
                parent->appendRow(newRow);
                const QString parentFilterString = parent->data(FilterRole).toString() + ' '
                                                   + newRow.first()->data(FilterRole).toString();
                parent->setData(QVariant(parentFilterString), FilterRole);
            } else {
                appendRow(newRow);
            }
        }
    }
}

void GerritModel::queryFinished()
{
    m_query->deleteLater();
    m_query = nullptr;
    setState(Idle);
    emit refreshStateChanged(false);
}

} // Gerrit::Internal

#include "gerritmodel.moc"
