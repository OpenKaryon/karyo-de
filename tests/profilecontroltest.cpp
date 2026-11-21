// SPDX-License-Identifier: GPL-3.0-or-later
#include "profilecontrol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ProfileControlTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    QString m_program;

    void mode(const QByteArray &value)
    {
        QFile file(m_root.filePath("mode"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(value), value.size());
    }
    QByteArray read(const QString &name)
    {
        QFile file(m_root.filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_program = m_root.filePath("karyo-desktops");
        QFile program(m_program);
        QVERIFY(program.open(QIODevice::WriteOnly));
        program.write(R"PY(#!/usr/bin/python3
import json, pathlib, sys, time
root=pathlib.Path(__file__).parent
mode=(root/'mode').read_text()
args=sys.argv[1:]
stdin=sys.stdin.buffer.read().decode()
with (root/'calls').open('a') as stream: stream.write(json.dumps({'args':args,'stdin':stdin})+'\n')
if mode in {'slow','hang'}: time.sleep(.15 if mode=='slow' else 3)
if mode=='flood': print('x'*300000); sys.exit(0)
if mode=='invalid': print('not JSON'); sys.exit(0)
if mode=='stopped': sys.exit(3)
if mode=='retained': sys.exit(2)
if mode=='failed': print('private native fixture diagnostic',file=sys.stderr); sys.exit(1)
if args[0]=='list':
 data=[{'user':'home','label':'Home','running':True,'active':True,'current':True,'vt':8}]
 if mode=='duplicate': data+=data
elif args[0]=='switch': data={'user':'wrong' if mode=='wrong' else args[1],'vt':9,'changed':True}
else: data={'user':'work'}
(root/'finished').write_text('done')
print(json.dumps(data))
)PY");
        program.close();
        QVERIFY(program.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qRegisterMetaType<ProfileCommand::Result>();
    }
    void init()
    {
        mode("ok");
        QFile::remove(m_root.filePath("calls"));
        QFile::remove(m_root.filePath("finished"));
    }
    void asynchronousListAndSharedRequest()
    {
        mode("slow");
        ProfileControl control(nullptr, m_program, 1000);
        QVERIFY(control.available());
        auto *job = control.list();
        QCOMPARE(control.list(), job);
        QJsonDocument response;
        connect(job, &ProfileCommand::finished, this, [&](auto) { response = job->response(); });
        QSignalSpy finished(job, &ProfileCommand::finished);
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(5);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<ProfileCommand::Result>(), ProfileCommand::Success);
        QVERIFY(ticks > 1);
        QCOMPARE(response.array().size(), 1);
        QCOMPARE(response.array().first().toObject().value("user").toString(), "home");
        QCOMPARE(read("calls").count('\n'), 1);
    }
    void stdinPasswordAndLiteralLabel()
    {
        ProfileControl control(nullptr, m_program);
        auto *job = control.add("--Experiments", "separate", "fixture private password");
        QSignalSpy finished(job, &ProfileCommand::finished);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<ProfileCommand::Result>(), ProfileCommand::Success);
        const auto call = QJsonDocument::fromJson(read("calls").trimmed()).object();
        const auto args = call.value("args").toArray();
        QVERIFY(args.contains("--label=--Experiments"));
        QVERIFY(args.contains("--password-stdin"));
        QVERIFY(!QJsonDocument(args).toJson().contains("fixture private password"));
        QCOMPARE(call.value("stdin").toString(), "fixture private password\n");
    }
    void sharedModeNeverSendsPassword()
    {
        ProfileControl control(nullptr, m_program);
        auto *job = control.add("Work", "none", "discarded fixture");
        QSignalSpy finished(job, &ProfileCommand::finished);
        QTRY_COMPARE(finished.count(), 1);
        const auto call = QJsonDocument::fromJson(read("calls").trimmed()).object();
        QCOMPARE(call.value("stdin").toString(), QString());
        QVERIFY(!call.value("args").toArray().contains("--password-stdin"));
    }
    void consumerLossDoesNotCancelMutation()
    {
        mode("slow");
        ProfileControl control(nullptr, m_program, 1000);
        auto *consumer = new QObject;
        auto *job = control.switchTo("work");
        connect(job, &ProfileCommand::finished, consumer, [] { });
        QSignalSpy finished(job, &ProfileCommand::finished);
        delete consumer;
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<ProfileCommand::Result>(), ProfileCommand::Success);
        QCOMPARE(read("finished"), "done");
        QVERIFY(!control.changing());
    }
    void duplicateMutationDoesNotStartAnotherProcess()
    {
        mode("slow");
        ProfileControl control(nullptr, m_program, 1000);
        auto *first = control.switchTo("work");
        QSignalSpy firstDone(first, &ProfileCommand::finished);
        QVERIFY(control.changing());
        auto *second = control.add("Another", "none", { });
        QSignalSpy secondDone(second, &ProfileCommand::finished);
        QTRY_COMPARE(secondDone.count(), 1);
        QCOMPARE(secondDone.first().first().value<ProfileCommand::Result>(), ProfileCommand::Busy);
        QVERIFY(control.changing());
        QTRY_COMPARE(firstDone.count(), 1);
        QCOMPARE(read("calls").count('\n'), 1);
        QVERIFY(!control.changing());
    }
    void errors_data()
    {
        QTest::addColumn<QByteArray>("fixture");
        QTest::addColumn<int>("operation");
        QTest::addColumn<ProfileCommand::Result>("expected");
        QTest::newRow("stopped") << QByteArray("stopped") << 1 << ProfileCommand::Stopped;
        QTest::newRow("retained") << QByteArray("retained") << 2 << ProfileCommand::Retained;
        QTest::newRow("failed") << QByteArray("failed") << 1 << ProfileCommand::Failed;
        QTest::newRow("wrong target") << QByteArray("wrong") << 1 << ProfileCommand::Uncertain;
        QTest::newRow("invalid mutation") << QByteArray("invalid") << 2 << ProfileCommand::Uncertain;
        QTest::newRow("invalid list") << QByteArray("invalid") << 0 << ProfileCommand::Failed;
        QTest::newRow("duplicate list") << QByteArray("duplicate") << 0 << ProfileCommand::Failed;
        QTest::newRow("output cap") << QByteArray("flood") << 1 << ProfileCommand::Uncertain;
        QTest::newRow("deadline") << QByteArray("hang") << 2 << ProfileCommand::Uncertain;
    }
    void errors()
    {
        QFETCH(QByteArray, fixture);
        QFETCH(int, operation);
        QFETCH(ProfileCommand::Result, expected);
        mode(fixture);
        ProfileControl control(nullptr, m_program, fixture == "hang" ? 30 : 1000);
        auto *job = operation == 0 ? control.list()
            : operation == 1       ? control.switchTo("work")
                                   : control.add("Work", "none", { });
        QString message;
        connect(job, &ProfileCommand::finished, this, [&](auto) { message = job->message(); });
        QSignalSpy finished(job, &ProfileCommand::finished);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<ProfileCommand::Result>(), expected);
        QVERIFY(!message.isEmpty());
        QVERIFY(!message.contains("private native fixture diagnostic"));
        QVERIFY(!control.changing());
    }
    void missingExecutableCompletesOnce()
    {
        ProfileControl control(nullptr, m_root.filePath("missing"));
        QVERIFY(!control.available());
        auto *job = control.list();
        QSignalSpy finished(job, &ProfileCommand::finished);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<ProfileCommand::Result>(), ProfileCommand::Failed);
    }
};

QTEST_GUILESS_MAIN(ProfileControlTest)
#include "profilecontroltest.moc"
