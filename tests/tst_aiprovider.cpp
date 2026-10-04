#include <QtTest>
#include "AiHttpFixture.h"
#include "ai/AiSecretStore.h"
#include "ai/PolzaProvider.h"
#include <QJsonArray>

class AiProviderTest : public QObject {
    Q_OBJECT
private slots:
    void authorizationAndDeletion();
    void streamedToolArguments();
    void malformedStreamNeverCompletes();
    void httpErrors_data();
    void httpErrors();
    void stopDiscardsLateReply();
    void cancellationPreventsRetry();
    void deadlineAbortsRequest();
};
void AiProviderTest::authorizationAndDeletion() {
    AiSecretStore secrets;
    QVERIFY(!secrets.save(QString())); QVERIFY(!secrets.save(QStringLiteral("test\r\nInjected: value")));
    QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture;
    fixture.handler = [](const auto &r) { AiHttpFixture::reply(r,200,"{\"label\":\"fixture\"}"); };
    PolzaProvider provider(&secrets,nullptr,fixture.base());
    QSignalSpy completed(&provider,&AiProvider::completed), failed(&provider,&AiProvider::failed);
    const auto first = provider.request(QStringLiteral("key"));
    QTRY_COMPARE(completed.count(),1);
    QCOMPARE(completed[0][0].toULongLong(),first);
    QCOMPARE(fixture.requests[0].authorization,QByteArray("Bearer pza_TEST_FIXTURE_ONLY"));
    provider.request(QStringLiteral("models")); QTRY_COMPARE(completed.count(),2);
    QVERIFY(fixture.requests[1].authorization.isEmpty());
    QVERIFY(secrets.remove()); QVERIFY(!secrets.hasKey()); QVERIFY(secrets.readForRequest().isEmpty());
    provider.request(QStringLiteral("key")); QTRY_COMPARE(failed.count(),1);
    QCOMPARE(failed[0][1].toString(),QStringLiteral("key_required"));
    QCOMPARE(fixture.requests.size(),2);
}
void AiProviderTest::streamedToolArguments() {
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture;
    fixture.handler = [](const auto &r) {
        const QByteArray sse =
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call1\",\"function\":{\"name\":\"set_project_setup\",\"arguments\":\"{\\\"width\\\":1080,\"}}]}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"height\\\":1920,\\\"fps\\\":30}\"}}]}}]}\n\n"
            "data: [DONE]\n\n";
        AiHttpFixture::reply(r,200,sse,"text/event-stream");
    };
    PolzaProvider provider(&secrets,nullptr,fixture.base()); QSignalSpy completed(&provider,&AiProvider::completed);
    provider.request(QStringLiteral("chat/completions"),{{QStringLiteral("model"),QStringLiteral("fixture/model")}},true);
    QTRY_COMPARE(completed.count(),1);
    QVERIFY(fixture.requests[0].json.value(QStringLiteral("stream")).toBool());
    const auto response = qvariant_cast<QJsonObject>(completed[0][1]);
    const auto call = response.value(QStringLiteral("choices")).toArray()[0].toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("tool_calls")).toArray()[0].toObject();
    QCOMPARE(call.value(QStringLiteral("id")).toString(),QStringLiteral("call1"));
    const auto fn = call.value(QStringLiteral("function")).toObject();
    QCOMPARE(fn.value(QStringLiteral("name")).toString(),QStringLiteral("set_project_setup"));
    QCOMPARE(QJsonDocument::fromJson(fn.value(QStringLiteral("arguments")).toString().toUtf8()).object().value(QStringLiteral("height")).toInt(),1920);
}
void AiProviderTest::malformedStreamNeverCompletes() {
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture; fixture.handler = [](const auto &r) { AiHttpFixture::reply(r,200,"data: {broken}\n\ndata: [DONE]\n\n","text/event-stream"); };
    PolzaProvider provider(&secrets,nullptr,fixture.base()); QSignalSpy completed(&provider,&AiProvider::completed), failed(&provider,&AiProvider::failed);
    provider.request(QStringLiteral("chat/completions"),{},true); QTRY_COMPARE(failed.count(),1);
    QCOMPARE(failed[0][1].toString(),QStringLiteral("malformed")); QCOMPARE(completed.count(),0);
}
void AiProviderTest::httpErrors_data() {
    QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body"); QTest::addColumn<QString>("code");
    QTest::newRow("invalid key") << 401 << QByteArray("private provider prose") << QStringLiteral("unauthorized");
    QTest::newRow("balance") << 402 << QByteArray("{}") << QStringLiteral("balance");
    QTest::newRow("access") << 403 << QByteArray("{}") << QStringLiteral("forbidden");
    QTest::newRow("rate limit") << 429 << QByteArray("{}") << QStringLiteral("rate_limit");
    QTest::newRow("server") << 500 << QByteArray("{}") << QStringLiteral("server");
    QTest::newRow("unsupported tools") << 400 << QByteArray("{\"error\":{\"message\":\"tools not supported\"}}") << QStringLiteral("unsupported_tools");
    QTest::newRow("missing model") << 404 << QByteArray("{\"error\":{\"code\":\"model_not_found\"}}") << QStringLiteral("model_unavailable");
}
void AiProviderTest::httpErrors() {
    QFETCH(int,status); QFETCH(QByteArray,body); QFETCH(QString,code);
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture; fixture.handler = [status,body](const auto &r) { AiHttpFixture::reply(r,status,body); };
    PolzaProvider provider(&secrets,nullptr,fixture.base()); QSignalSpy failed(&provider,&AiProvider::failed), logs(&provider,&AiProvider::diagnostic);
    provider.request(QStringLiteral("key")); QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000); QCOMPARE(failed[0][1].toString(),code);
    for (const auto &row : logs) { QVERIFY(!row[0].toString().contains(QStringLiteral("pza_TEST"))); QVERIFY(!row[0].toString().contains(QString::fromUtf8(body))); }
}
void AiProviderTest::stopDiscardsLateReply() {
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture; PolzaProvider provider(&secrets,nullptr,fixture.base());
    QSignalSpy completed(&provider,&AiProvider::completed), failed(&provider,&AiProvider::failed);
    provider.request(QStringLiteral("key")); QTRY_COMPARE(fixture.requests.size(),1);
    provider.cancel(); AiHttpFixture::reply(fixture.requests[0],200,"{\"label\":\"late\"}"); QTest::qWait(100);
    QCOMPARE(completed.count(),0); QCOMPARE(failed.count(),0);
}
void AiProviderTest::cancellationPreventsRetry() {
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture; fixture.handler = [](const auto &r) { AiHttpFixture::reply(r,429,"{}"); };
    PolzaProvider provider(&secrets,nullptr,fixture.base());
    provider.request(QStringLiteral("key")); QTRY_COMPARE(fixture.requests.size(),1);
    QTest::qWait(100); provider.cancel(); QVERIFY(secrets.remove()); QTest::qWait(2200);
    QCOMPARE(fixture.requests.size(),1);
}
void AiProviderTest::deadlineAbortsRequest() {
    AiSecretStore secrets; QVERIFY(secrets.save(QStringLiteral("pza_TEST_FIXTURE_ONLY")));
    AiHttpFixture fixture; PolzaProvider provider(&secrets,nullptr,fixture.base(),100);
    QSignalSpy failed(&provider,&AiProvider::failed), completed(&provider,&AiProvider::completed);
    provider.request(QStringLiteral("key")); QTRY_COMPARE(failed.count(),1);
    QCOMPARE(failed[0][1].toString(),QStringLiteral("timeout")); QCOMPARE(completed.count(),0);
}
QTEST_GUILESS_MAIN(AiProviderTest)
#include "tst_aiprovider.moc"
