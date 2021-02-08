#include "pch.h"

using namespace WinToastLib;

class MyReceiver : public QObject
{
    Q_OBJECT
public slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
};

class CustomHandler : public IWinToastHandler {
public:
    CustomHandler(const QString& server, int id) : m_server(server), m_id(id) {}

    void toastActivated() const {}

    void toastActivated(int actionIndex) const
    {
        if (actionIndex == 0)
            QDesktopServices::openUrl(QString("%1/issues/%2").arg(m_server).arg(m_id));
    }

    void toastFailed() const {}
    void toastDismissed(WinToastDismissalReason) const {}
private:
    QString m_server;
    int m_id;
};

struct Issue
{
    int id;
    QString subject;
    QDateTime updated_on;
};

struct Settings
{
    QString api_key;
    QString server;
    int interval;

    bool track_assigned_to_me = true;
    bool track_my = true;
    bool track_watched = true;

    QDateTime last_update_on;
    QSet<int> updated_at_last_sec;
};

QVector<Issue> extract_new_issues(const QString& xml)
{
    QVector<Issue> res;

    QDomDocument doc;
    if (!doc.setContent(xml))
        return res;

    QDomElement issues = doc.documentElement();
    QString s1 = issues.nodeName();
    QDomNode issue_node = issues.firstChild();
    while (!issue_node.isNull())
    {
        Issue issue;
        issue.id = issue_node.firstChildElement("id").text().toInt();
        issue.subject = issue_node.firstChildElement("subject").text();
        QString updated_on_str = issue_node.firstChildElement("updated_on").text();
        issue.updated_on = QDateTime::fromString(updated_on_str, Qt::DateFormat::ISODate);
        res.push_back(issue);

        issue_node = issue_node.nextSibling();
    }

    return res;
}

Settings load_settings()
{
    Settings result;
    QSettings settings("settings.ini", QSettings::Format::IniFormat);

    result.api_key = settings.value("main/api_key").toString();
    result.server = settings.value("main/server").toString();
    result.interval = settings.value("main/interval").toInt();

    result.track_assigned_to_me = settings.value("main/track_assigned_to_me").toBool();
    result.track_my = settings.value("main/track_my").toBool();
    result.track_watched = settings.value("main/track_watched").toBool();

    QString last_update_on_str = settings.value("session/last_update_on").toString();
    result.last_update_on = QDateTime::fromString(last_update_on_str, Qt::DateFormat::ISODate);

    QStringList ids = settings.value("session/updated_at_last_sec").toStringList();
    for (const QString& id : ids)
        result.updated_at_last_sec.insert(id.toInt());

    if (result.interval < 1)
        result.interval = 60;

    return result;
}

void save_settings(const Settings& settings)
{
    QSettings s("settings.ini", QSettings::Format::IniFormat);
    if (settings.last_update_on.isValid())
        s.setValue("session/last_update_on", settings.last_update_on.toString(Qt::DateFormat::ISODate));
    
    QStringList ids;
    for (int id : settings.updated_at_last_sec)
        ids.push_back(QString::number(id));
    if (!ids.empty())
        s.setValue("session/updated_at_last_sec", ids);
}

void show_notifications(const QString& server, const QMap<int, Issue>& issue_map)
{
    for (const Issue& issue : issue_map)
    {
        WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText01);
        templ.setTextField(issue.subject.toStdWString(), WinToastTemplate::FirstLine);
        QString img = QDir::currentPath() + "/redmine.png";
        templ.setImagePath(img.toStdWString());
        templ.addAction(L"Open");
        templ.addAction(L"Close");
        WinToast::instance()->showToast(templ, new CustomHandler(server, issue.id));
    }
}

class TimerTask
{
public:
    TimerTask(Settings& settings) : m_settings(settings) {}
    void operator()()
    {
        QNetworkAccessManager manager;
        QList<QNetworkReply*> replies;
        QStringList args;
        if (m_settings.track_assigned_to_me)
            args.push_back("assigned_to_id");
        if (m_settings.track_my)
            args.push_back("author_id");
        if (m_settings.track_watched)
            args.push_back("watcher_id");

        QEventLoop loop;
        int total = args.size();

        for (const QString& arg : args)
        {
            QString url = QString("%1/issues.xml?key=%2&sort=updated_on:desc&%3=me")
                .arg(m_settings.server, m_settings.api_key, arg);
            if (!m_settings.last_update_on.isNull())
            {
                url += "&updated_on=%3E%3D" + m_settings.last_update_on.toString(Qt::DateFormat::ISODate);
            }

            QNetworkReply* reply = manager.get(QNetworkRequest(QUrl(url)));
            QObject::connect(reply, &QNetworkReply::finished, [&total, &loop]()
            {
                total--;
                if (total == 0)
                    loop.quit();
            });
            replies.push_back(reply);
        }

        loop.exec();

        QVector<Issue> all_issues;
        for (QNetworkReply* reply : replies)
        {
            if (reply->error() == QNetworkReply::NetworkError::NoError)
            {
                QString answer = reply->readAll();
                all_issues.append(extract_new_issues(answer));
            }
        }

        qDeleteAll(replies.begin(), replies.end());
        replies.clear();

        QMap<int, Issue> issue_map;

        for (const Issue& issue : all_issues)
        {
            if (!m_settings.updated_at_last_sec.contains(issue.id) || issue.updated_on > m_settings.last_update_on)
            {
                issue_map[issue.id] = issue;
            }
        }

        QDateTime new_last_update_on = m_settings.last_update_on;
        QSet<int> new_updated_at_last_sec;

        for (const Issue& issue : all_issues)
        {
            if (new_last_update_on < issue.updated_on)
                new_last_update_on = issue.updated_on;
        }

        for (const Issue& issue : all_issues)
        {
            if (new_last_update_on == issue.updated_on)
                new_updated_at_last_sec.insert(issue.id);
        }

        if (new_last_update_on != m_settings.last_update_on ||
            new_updated_at_last_sec != m_settings.updated_at_last_sec)
        {
            m_settings.last_update_on = new_last_update_on;
            m_settings.updated_at_last_sec = new_updated_at_last_sec;
            save_settings(m_settings);
        }

        show_notifications(m_settings.server, issue_map);
    };
private:
    Settings& m_settings;
};

int main(int argc, char* argv[])
{
    WinToast::instance()->setAppName(L"SimpleRedmineNotifier");
    WinToast::instance()->setAppUserModelId(L"SimpleRedmineNotifier");
    bool wintoast_init_res = WinToast::instance()->initialize();

    QApplication app(argc, argv);

    QPixmap pixmap(32, 32);
    pixmap.load("icon.ico");
    QIcon icon(pixmap);
    QSystemTrayIcon* tray_icon = new QSystemTrayIcon(icon);
    QMenu* tray_menu = new QMenu();
    QAction* quit_action = new QAction("Quit", tray_menu);
    QObject::connect(quit_action, &QAction::triggered, qApp, &QCoreApplication::quit);
    tray_menu->addAction(quit_action);
    tray_icon->setContextMenu(tray_menu);
    tray_icon->show();

    Settings settings = load_settings();

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, TimerTask(settings));
    timer.start(settings.interval * 1000);

    bool res = app.exec();

    tray_icon->hide();

    delete tray_menu;
    delete tray_icon;

    return res;
}