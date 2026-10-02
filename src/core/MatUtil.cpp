#include "MatUtil.h"
#include <opencv2/imgproc.hpp>
#include <QPainter>
#include <cstring>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(MatUtil) }; }   // traductions hors classes QObject (voir translations/)

namespace mu {
QImage toQImage(const cv::Mat& m) {
    QImage img(m.cols, m.rows, QImage::Format_ARGB32);
    for (int y = 0; y < m.rows; ++y) std::memcpy(img.scanLine(y), m.ptr(y), size_t(m.cols) * 4);
    return img;
}

cv::Mat fromQImage(const QImage& in) {
    QImage img = in.convertToFormat(QImage::Format_ARGB32);
    cv::Mat m(img.height(), img.width(), CV_8UC4);
    for (int y = 0; y < m.rows; ++y) std::memcpy(m.ptr(y), img.constScanLine(y), size_t(m.cols) * 4);
    return m;
}

QImage thumbnail(const cv::Mat& m, QSize box, bool checker) {
    if (m.empty()) return QImage();
    double s = std::min(double(box.width()) / m.cols, double(box.height()) / m.rows);
    cv::Size ns(std::max(1, int(m.cols * s)), std::max(1, int(m.rows * s)));
    cv::Mat small;
    cv::resize(m, small, ns, 0, 0, cv::INTER_AREA);
    QImage out(ns.width, ns.height, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    if (checker) {
        for (int y = 0; y < ns.height; y += 6)
            for (int x = 0; x < ns.width; x += 6)
                p.fillRect(x, y, 6, 6, ((x / 6 + y / 6) & 1) ? QColor(150, 150, 150) : QColor(220, 220, 220));
    }
    p.drawImage(0, 0, toQImage(small));
    return out;
}

void premultiply(cv::Mat& m) {
#pragma omp parallel for
    for (int y = 0; y < m.rows; ++y) {
        cv::Vec4b* p = m.ptr<cv::Vec4b>(y);
        for (int x = 0; x < m.cols; ++x) {
            int a = p[x][3];
            p[x][0] = uchar((p[x][0] * a + 127) / 255);
            p[x][1] = uchar((p[x][1] * a + 127) / 255);
            p[x][2] = uchar((p[x][2] * a + 127) / 255);
        }
    }
}

void unpremultiply(cv::Mat& m) {
#pragma omp parallel for
    for (int y = 0; y < m.rows; ++y) {
        cv::Vec4b* p = m.ptr<cv::Vec4b>(y);
        for (int x = 0; x < m.cols; ++x) {
            int a = p[x][3];
            if (a == 0 || a == 255) continue;
            for (int c = 0; c < 3; ++c) p[x][c] = uchar(std::min(255, (p[x][c] * 255 + a / 2) / a));
        }
    }
}

QString humanSize(qint64 bytes) {
    if (bytes < 0) return "?";
    static const char* units[] = {"o", QT_TRANSLATE_NOOP("MatUtil", "Ko"), QT_TRANSLATE_NOOP("MatUtil", "Mo"), QT_TRANSLATE_NOOP("MatUtil", "Go"), QT_TRANSLATE_NOOP("MatUtil", "To")};
    double v = double(bytes);
    int u = 0;
    while (v >= 1000.0 && u < 4) { v /= 1000.0; ++u; }
    return (u == 0) ? Tr::tr("%1 o").arg(bytes) : QString("%1 %2").arg(v, 0, 'f', v < 10 ? 2 : (v < 100 ? 1 : 0)).arg(Tr::tr(units[u]));
}
}
