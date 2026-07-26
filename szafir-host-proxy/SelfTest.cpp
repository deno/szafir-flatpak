#include "SelfTest.h"
#include "BwrapSandbox.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

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

} // namespace SelfTest
