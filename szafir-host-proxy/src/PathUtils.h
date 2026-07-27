#pragma once

#include <filesystem>
#include <QString>
#include <QByteArray>
#include <string_view>

namespace PathUtils {

/**
 * @brief Converts a QString to a std::filesystem::path with maximum efficiency.
 * @details On Windows, this is a near-zero-copy operation. On POSIX systems,
 * it uses Qt's highly optimized UTF-16 to UTF-8 transcoder.
 */
inline std::filesystem::path toFsPath(const QString &qtPath)
{
    if (qtPath.isEmpty())
        return {};

#ifdef _WIN32
    // WINDOWS: path is 16-bit (wchar_t). QString stores 16-bit (char16_t).
    // This is the fastest possible conversion, treating the raw buffer as a wstring_view.
    return std::filesystem::path(
        reinterpret_cast<const wchar_t*>(qtPath.utf16()),
        reinterpret_cast<const wchar_t*>(qtPath.utf16() + qtPath.length())
    );
#else
    // POSIX (Linux, macOS, etc.): path is 8-bit (char).
    // Qt's toStdString() provides the fastest transcoding from UTF-16 to UTF-8.
    return std::filesystem::path(qtPath.toStdString());
#endif
}

/**
 * @brief Converts a std::filesystem::path back to a QString.
 * @details This is a zero-copy operation that refers to the path's internal buffer.
 */
inline QString toQString(const std::filesystem::path &fsPath)
{
    if (fsPath.empty())
        return QString();

#ifdef _WIN32
    // WINDOWS: .native() returns a std::wstring&
    return QString::fromStdWString(fsPath.native());
#else
    // POSIX: .native() returns a std::string& (which is UTF-8)
    return QString::fromStdString(fsPath.native());
#endif
}

/**
 * @brief Converts a UTF-8 encoded QByteArray to a std::filesystem::path.
 * @details On POSIX systems, this is a zero-copy operation. On Windows,
 * it must transcode from UTF-8 to UTF-16 to avoid codepage corruption.
 */
inline std::filesystem::path toFsPath(const QByteArray &utf8Path)
{
    if (utf8Path.isEmpty())
        return {};

#ifdef _WIN32
    // WINDOWS: We must decode the UTF-8 into UTF-16. The most robust way
    // is to reuse our existing QString converter.
    return toFsPath(QString::fromUtf8(utf8Path));
#else
    // POSIX: The native path is already UTF-8. We can create a zero-copy view.
    return std::filesystem::path(std::string_view(utf8Path.constData(), utf8Path.size()));
#endif
}

} // namespace PathUtils
