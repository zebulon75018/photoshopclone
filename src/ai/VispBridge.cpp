#include "VispBridge.h"
#ifdef PC_HAVE_VISIONCPP
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(VispBridge) }; }   // traductions hors classes QObject (voir translations/)

namespace ai {
using namespace visp;

image_view viewOf(const cv::Mat& m) {
    image_format fmt;
    if (m.type() == CV_8UC4) fmt = image_format::bgra_u8;
    else if (m.type() == CV_8UC1) fmt = image_format::alpha_u8;
    else throw std::runtime_error(Tr::tr("viewOf : format cv::Mat non pris en charge (CV_8UC4 ou CV_8UC1 attendu)").toStdString());
    image_view v({m.cols, m.rows}, fmt, static_cast<uint8_t const*>(m.data));
    v.stride = int(m.step[0]);   // respecte une éventuelle sous-image (ROI) non contiguë
    return v;
}

cv::Mat toMatBGRA(const image_data& img) {
    const int w = img.extent[0], h = img.extent[1];
    cv::Mat out(h, w, CV_8UC4);
    auto* raw = const_cast<uint8_t*>(img.data.get());
    switch (img.format) {
    case image_format::bgra_u8: {
        cv::Mat(h, w, CV_8UC4, raw).copyTo(out);
        break;
    }
    case image_format::rgba_u8: {
        cv::cvtColor(cv::Mat(h, w, CV_8UC4, raw), out, cv::COLOR_RGBA2BGRA);
        break;
    }
    case image_format::rgb_u8: {
        cv::cvtColor(cv::Mat(h, w, CV_8UC3, raw), out, cv::COLOR_RGB2BGRA);
        break;
    }
    case image_format::argb_u8: {
        cv::Mat src(h, w, CV_8UC4, raw);
        int from_to[] = {0, 3, 1, 2, 2, 1, 3, 0};   // ARGB(A,R,G,B) -> BGRA(B,G,R,A)
        cv::mixChannels(&src, 1, &out, 1, from_to, 4);
        break;
    }
    case image_format::alpha_u8: {
        cv::Mat bgr;
        cv::cvtColor(cv::Mat(h, w, CV_8UC1, raw), bgr, cv::COLOR_GRAY2BGR);
        cv::cvtColor(bgr, out, cv::COLOR_BGR2BGRA);
        break;
    }
    default:
        throw std::runtime_error(Tr::tr("toMatBGRA : format image_data non pris en charge (flottant inattendu)").toStdString());
    }
    return out;
}

cv::Mat toMatGray(const image_data& img) {
    const int w = img.extent[0], h = img.extent[1];
    cv::Mat out(h, w, CV_8UC1);
    auto* raw = const_cast<uint8_t*>(img.data.get());
    if (img.format == image_format::alpha_u8) {
        cv::Mat(h, w, CV_8UC1, raw).copyTo(out);
    } else if (img.format == image_format::alpha_f32) {
        cv::Mat(h, w, CV_32FC1, raw).convertTo(out, CV_8UC1, 255.0, 0.0);
    } else {
        throw std::runtime_error(Tr::tr("toMatGray : format image_data non pris en charge (alpha_u8 ou alpha_f32 attendu)").toStdString());
    }
    return out;
}

}
#endif
