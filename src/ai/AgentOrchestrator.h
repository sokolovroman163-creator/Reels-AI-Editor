#pragma once
#include "AiSecretStore.h"
#include "PolzaProvider.h"
#include "McpAiBridge.h"
#include <QObject>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
class AppController;

class AgentOrchestrator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool keySaved READ keySaved NOTIFY stateChanged)
    Q_PROPERTY(bool persistentKey READ persistentKey CONSTANT)
    Q_PROPERTY(bool checkingConnection READ checkingConnection NOTIFY stateChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY stateChanged)
    Q_PROPERTY(QString modelChoice READ modelChoice WRITE setModelChoice NOTIFY stateChanged)
    Q_PROPERTY(QString customModel READ customModel WRITE setCustomModel NOTIFY stateChanged)
    Q_PROPERTY(int maxAgentSteps READ maxAgentSteps WRITE setMaxAgentSteps NOTIFY stateChanged)
    Q_PROPERTY(bool developerLogs READ developerLogs WRITE setDeveloperLogs NOTIFY stateChanged)
    Q_PROPERTY(QStringList logs READ logs NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool reviewingPlan READ reviewingPlan NOTIFY stateChanged)
    Q_PROPERTY(QString plan READ plan NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(QString result READ result NOTIFY stateChanged)
    Q_PROPERTY(QString activeModel READ activeModel NOTIFY stateChanged)
    Q_PROPERTY(QString agentMode READ agentMode NOTIFY stateChanged)
    Q_PROPERTY(int step READ step NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
public:
    explicit AgentOrchestrator(AppController *controller, QObject *parent = nullptr,
        const QUrl &providerBase = QUrl(QStringLiteral("https://polza.ai/api/v1/")));
    bool keySaved() const { return m_secrets.hasKey(); }
    bool persistentKey() const;
    bool checkingConnection() const { return m_checking; }
    QString connectionStatus() const { return m_connection; }
    QString modelChoice() const { return m_choice; }
    void setModelChoice(const QString &choice);
    QString customModel() const { return m_custom; }
    void setCustomModel(const QString &model);
    int maxAgentSteps() const { return m_maxSteps; }
    void setMaxAgentSteps(int steps);
    bool developerLogs() const { return m_debug; }
    void setDeveloperLogs(bool enabled);
    QStringList logs() const { return m_logs; }
    bool busy() const { return m_active || m_review || m_stopping || m_localCall; }
    bool reviewingPlan() const { return m_review; }
    QString plan() const { return m_plan; }
    QString stage() const { return m_stage; }
    QString result() const { return m_result; }
    QString activeModel() const { return m_model; }
    QString agentMode() const;
    int step() const { return m_step; }
    bool canUndo() const;
    Q_INVOKABLE bool saveKey(const QString &key);
    Q_INVOKABLE void deleteKey();
    Q_INVOKABLE void checkConnection();
    Q_INVOKABLE void start(const QString &prompt, const QString &preset, int seconds,
        const QString &aspect, const QString &captions, bool showPlan, bool quality,
        bool previews, const QStringList &assets);
    Q_INVOKABLE void assemblePlan();
    Q_INVOKABLE void changePlan();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void undoMontage();
signals:
    void stateChanged();
private:
    void received(quint64 id, const QJsonObject &response);
    void failure(quint64 id, const QString &code, const QString &message);
    bool guard(bool revision = true);
    bool configureModel();
    void prepareContext();
    void prepareFrame(int index, quint64 session);
    void next();
    void execute(QJsonArray commands, const QJsonArray &calls);
    void appendOutput(const QString &tool, const QJsonObject &out, const QString &callId = {});
    void pollJobs();
    bool jobsActive() const;
    void cancelJobs();
    void restore();
    void finish(const QString &message, bool success = false);
    void reportError(const QString &tool, const QJsonObject &error);
    QJsonObject restrictCommand(QJsonObject &command) const;
    bool timelineChanged() const;
    void log(const QString &line);
    AppController *m_controller;
    AiSecretStore m_secrets;
    PolzaProvider m_provider;
    McpAiBridge m_bridge;
    QTimer m_jobTimer;
    QElapsedTimer m_jobWait;
    QJsonArray m_messages, m_models, m_pendingImages;
    QStringList m_assets, m_logs;
    QSet<QString> m_ownedJobs;
    QSet<QString> m_subtitlesBefore;
    QString m_choice, m_custom, m_model, m_connection, m_stage, m_result, m_plan;
    QString m_projectId, m_projectPath, m_snapshot, m_lastError, m_captions;
    int m_maxSteps = 18, m_step = 0, m_revision = 0, m_errors = 0, m_invalidPlans = 0, m_mutations = 0;
    quint64 m_request = 0, m_session = 0;
    bool m_debug = false, m_checking = false, m_active = false, m_review = false;
    bool m_stopping = false, m_localCall = false, m_undoPending = false, m_ownedSubtitle = false;
    bool m_native = true, m_vision = false, m_previews = false, m_planFirst = false;
    bool m_describingPlan = false, m_upgraded = false;
};
