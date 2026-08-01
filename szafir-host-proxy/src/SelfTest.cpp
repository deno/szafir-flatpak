#include "SelfTest.h"
#include "BwrapSandbox.h"
#include "LandlockEnv.h"
#include "LandlockSandbox.h"
#include "Pkcs11Probe.h"
#include "SecureNetwork.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <winscard.h>

namespace {

// Create a pipe with both ends close-on-exec. The child's fork handler dup2()s the
// needed ends onto stdin/stdout (which clears CLOEXEC on those two), so the remaining
// pipe FDs are closed on exec and the child does not leak a copy of the write end
// (which would prevent the reader from ever seeing EOF).
int makePipe(int fds[2])
{
    if (pipe(fds) < 0)
        return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

bool writeAll(int fd, const char *data, size_t len)
{
    while (len > 0) {
        const ssize_t n = ::write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

} // namespace

namespace SelfTest {

int fdPassthrough(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (!BwrapSandbox::childWrappingEnabled()) {
        printf("selftest-fd: SKIP (bwrap child wrapping not enabled)\n");
        return 0;
    }

    // Pipes standing in for the browser's native-messaging FDs.
    int inPipe[2];  // parent -> child (child's stdin)
    int outPipe[2]; // child -> parent (child's stdout)
    if (makePipe(inPipe) < 0 || makePipe(outPipe) < 0) {
        perror("selftest-fd: pipe");
        return 1;
    }

    // Same argument construction as NativeMessagingService::spawnHost, but with
    // /usr/bin/cat standing in for SzafirHost: it echoes stdin to stdout. /usr/bin/cat
    // is an ELF under /usr, which the launcher Landlock permits (read_exec), so the
    // test exercises the chain with Landlock ON and no shebang involved.
    const std::vector<std::string> bwrapArgs = BwrapSandbox::childSandboxArgs(
        BwrapSandbox::selfExePath(), "/usr/bin/cat", {});

    QStringList qArgs;
    qArgs.reserve(static_cast<int>(bwrapArgs.size()));
    for (const std::string &a : bwrapArgs)
        qArgs.append(QString::fromStdString(a));

    QProcess process;
    process.setProgram(QString::fromStdString(BwrapSandbox::bwrapPath()));
    process.setArguments(qArgs);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    const int childStdinRead = inPipe[0];
    const int childStdoutWrite = outPipe[1];
    process.setChildProcessModifier([childStdinRead, childStdoutWrite]() {
        if (dup2(childStdinRead, STDIN_FILENO) < 0)
            _exit(127);
        if (dup2(childStdoutWrite, STDOUT_FILENO) < 0)
            _exit(127);
    });

    process.start();
    if (!process.waitForStarted(5000)) {
        fprintf(stderr, "selftest-fd: FAIL (process did not start: %s)\n",
                qPrintable(process.errorString()));
        return 1;
    }

    // Close the child's pipe ends in the parent. Closing outPipe[1] here is required
    // so the read below sees EOF once the child exits.
    close(inPipe[0]);
    close(outPipe[1]);

    // Send a message, then close the write end so cat sees EOF and exits.
    static const char msg[] = "szafir-fd-passthrough\n";
    const bool wroteOk = writeAll(inPipe[1], msg, sizeof(msg) - 1);
    close(inPipe[1]);
    if (!wroteOk) {
        fprintf(stderr, "selftest-fd: FAIL (write to child stdin failed)\n");
        process.kill();
        process.waitForFinished(2000);
        return 1;
    }

    if (!process.waitForFinished(15000)) {
        fprintf(stderr, "selftest-fd: FAIL (child did not exit in time)\n");
        process.kill();
        process.waitForFinished(2000);
        return 1;
    }

    // Drain the echoed bytes from the child's stdout.
    std::string echoed;
    char buf[256];
    for (;;) {
        const ssize_t n = read(outPipe[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        echoed.append(buf, static_cast<size_t>(n));
    }
    close(outPipe[0]);

    const QByteArray stderrOut = process.readAllStandardError();
    const std::string expected(msg, sizeof(msg) - 1);

    if (echoed == expected && process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0) {
        printf("selftest-fd: PASS (message round-tripped through bwrap + --launch-host + Landlock)\n");
        return 0;
    }

    fprintf(stderr, "selftest-fd: FAIL (exitCode=%d exitStatus=%d, echoed %zu of %zu bytes)\n",
            process.exitCode(), static_cast<int>(process.exitStatus()),
            echoed.size(), expected.size());
    if (!stderrOut.isEmpty())
        fprintf(stderr, "selftest-fd: child stderr:\n%s\n", stderrOut.constData());
    return 1;
}

int tlsProbe(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Mirror main(): Phase 1 before any Qt networking, Phase 2 afterwards.
    if (LandlockEnv::isModuleEnabled("LANDLOCK_PHASE_1")) {
        if (!LandlockSandbox::limitOverrides()) {
            fprintf(stderr, "selftest-tls: FAIL (Landlock Phase 1 failed)\n");
            return 1;
        }
    } else {
        printf("selftest-tls: Landlock Phase 1 disabled by environment\n");
    }
    if (LandlockEnv::isModuleEnabled("LANDLOCK_PHASE_2")) {
        if (!LandlockSandbox::dropBrowserAccess()) {
            fprintf(stderr, "selftest-tls: FAIL (Landlock Phase 2 failed)\n");
            return 1;
        }
    } else {
        printf("selftest-tls: Landlock Phase 2 disabled by environment\n");
    }

    printf("selftest-tls: backend=%s library=%s supportsSsl=%d\n",
           qPrintable(QSslSocket::activeBackend()),
           qPrintable(QSslSocket::sslLibraryVersionString()),
           QSslSocket::supportsSsl());
    printf("selftest-tls: defaultConfiguration CA certs=%lld systemCaCertificates=%lld\n",
           static_cast<long long>(QSslConfiguration::defaultConfiguration().caCertificates().size()),
           static_cast<long long>(QSslConfiguration::systemCaCertificates().size()));
    const char *sslCertFile = getenv("SSL_CERT_FILE");
    printf("selftest-tls: SSL_CERT_FILE=%s\n", sslCertFile ? sslCertFile : "(unset)");
    fflush(stdout);

    const QUrl url(argc > 2 ? QString::fromLocal8Bit(argv[2])
                            : QStringLiteral("https://www.elektronicznypodpis.pl/"));

    QNetworkAccessManager manager;
    QNetworkRequest request = SecureNetwork::makeSecureRequest(url);
    if (!request.url().isValid()) {
        fprintf(stderr, "selftest-tls: FAIL (URL rejected: %s)\n", qPrintable(url.toString()));
        return 1;
    }

    QNetworkReply *reply = manager.head(request);
    SecureNetwork::attachSslAbort(reply);

    bool sawSslError = false;
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [&sawSslError](const QList<QSslError> &errors) {
            sawSslError = true;
            for (const QSslError &e : errors)
                fprintf(stderr, "selftest-tls: sslError: %s\n", qPrintable(e.errorString()));
        });

    int result = 1;
    QObject::connect(reply, &QNetworkReply::finished, &app,
        [&app, &result, &sawSslError, reply]() {
            const bool tlsOk = !sawSslError && reply->error() != QNetworkReply::SslHandshakeFailedError
                               && reply->error() != QNetworkReply::OperationCanceledError;
            if (tlsOk) {
                printf("selftest-tls: PASS (handshake OK, HTTP status %d, error=%d %s)\n",
                       reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                       static_cast<int>(reply->error()), qPrintable(reply->errorString()));
                result = 0;
            } else {
                fprintf(stderr, "selftest-tls: FAIL (error=%d %s)\n",
                        static_cast<int>(reply->error()), qPrintable(reply->errorString()));
            }
            app.quit();
        });

    QTimer::singleShot(20000, &app, [&app]() {
        fprintf(stderr, "selftest-tls: FAIL (timeout)\n");
        app.quit();
    });

    app.exec();
    return result;
}

int pcscProbe(int argc, char *argv[])
{
    SCARDCONTEXT ctx = 0;
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &ctx);
    if (rv == SCARD_E_NO_SERVICE || rv == SCARD_E_SERVICE_STOPPED) {
        printf("selftest-pcsc: SKIP (no pcscd running)\n");
        return 0;
    }
    if (rv != SCARD_S_SUCCESS) {
        fprintf(stderr, "selftest-pcsc: FAIL (SCardEstablishContext: 0x%08lX)\n",
                static_cast<unsigned long>(rv));
        return 1;
    }

    DWORD len = 0;
    rv = SCardListReaders(ctx, nullptr, nullptr, &len);
    if (rv == SCARD_E_NO_READERS_AVAILABLE) {
        printf("selftest-pcsc: PASS (daemon reachable, no readers attached)\n");
        SCardReleaseContext(ctx);
        return 0;
    }
    if (rv != SCARD_S_SUCCESS) {
        fprintf(stderr, "selftest-pcsc: FAIL (SCardListReaders: 0x%08lX)\n",
                static_cast<unsigned long>(rv));
        SCardReleaseContext(ctx);
        return 1;
    }

    std::vector<char> buf(len);
    rv = SCardListReaders(ctx, nullptr, buf.data(), &len);
    if (rv != SCARD_S_SUCCESS) {
        fprintf(stderr, "selftest-pcsc: FAIL (SCardListReaders fill: 0x%08lX)\n",
                static_cast<unsigned long>(rv));
        SCardReleaseContext(ctx);
        return 1;
    }

    std::vector<SCARD_READERSTATE> states;
    for (const char *p = buf.data(); *p; p += strlen(p) + 1) {
        SCARD_READERSTATE rs{};
        rs.szReader = p;
        rs.dwCurrentState = SCARD_STATE_UNAWARE;
        states.push_back(rs);
    }

    rv = SCardGetStatusChange(ctx, 0, states.data(), static_cast<DWORD>(states.size()));
    if (rv != SCARD_S_SUCCESS && rv != SCARD_E_TIMEOUT) {
        fprintf(stderr, "selftest-pcsc: FAIL (SCardGetStatusChange: 0x%08lX)\n",
                static_cast<unsigned long>(rv));
        SCardReleaseContext(ctx);
        return 1;
    }

    for (const auto &rs : states)
        printf("selftest-pcsc: reader \"%s\" cardPresent=%d\n",
               rs.szReader, (rs.dwEventState & SCARD_STATE_PRESENT) != 0);

    printf("selftest-pcsc: PASS (%zu reader(s))\n", states.size());
    SCardReleaseContext(ctx);
    return 0;
}

int pkcs11Probe(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (LandlockEnv::isModuleEnabled("LANDLOCK_PHASE_1")) {
        if (!LandlockSandbox::limitOverrides()) {
            fprintf(stderr, "selftest-pkcs11: FAIL (Landlock Phase 1 failed)\n");
            return 1;
        }
    }
    if (LandlockEnv::isModuleEnabled("LANDLOCK_PHASE_2")) {
        if (!LandlockSandbox::dropBrowserAccess()) {
            fprintf(stderr, "selftest-pkcs11: FAIL (Landlock Phase 2 failed)\n");
            return 1;
        }
    }

    if (argc < 3 || argv[2][0] == '\0') {
        fprintf(stderr, "selftest-pkcs11: FAIL (provider path is required)\n");
        return 1;
    }

    const QJsonObject result = Pkcs11Probe::run(QString::fromLocal8Bit(argv[2]));
    printf("selftest-pkcs11: %s\n",
           QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

} // namespace SelfTest
