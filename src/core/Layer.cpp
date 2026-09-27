#include "Layer.h"
#include "MatUtil.h"
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <atomic>

static std::atomic<int> g_nextLayerId{1};

Layer::Ptr Layer::create(const QString& name, const cv::Mat& bgra) {
    Ptr l(new Layer);
    l->m_id = g_nextLayerId++;
    l->props.name = name;
    l->image = bgra;
    return l;
}

Layer::Ptr Layer::createEmpty(const QString& name, QSize size) { return create(name, mu::newMat(size)); }

Layer::Ptr Layer::clone() const {
    Ptr l(new Layer);
    l->m_id = g_nextLayerId++;
    l->props = props;
    l->image = image.clone();
    l->mask = mask.clone();
    l->text = text;
    return l;
}

static QFont fontOf(const TextData& t) {
    QFont f(t.family);
    f.setPixelSize(std::max(1, t.pixelSize));
    f.setBold(t.bold);
    f.setItalic(t.italic);
    f.setUnderline(t.underline);
    return f;
}

QRectF Layer::textBounds() const {
    if (!text) return {};
    QFontMetricsF fm(fontOf(*text));
    QRectF br = fm.boundingRect(QRectF(0, 0, 100000, 100000), Qt::AlignLeft | Qt::TextDontClip, text->text);
    return QRectF(text->pos, QSizeF(br.width() + 2, br.height() + 2));
}

void Layer::renderText() {
    if (!text) return;
    QImage img(image.cols, image.rows, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        p.setFont(fontOf(*text));
        p.setPen(text->color);
        Qt::Alignment al = text->align == 1 ? Qt::AlignHCenter : text->align == 2 ? Qt::AlignRight : Qt::AlignLeft;
        p.drawText(textBounds(), int(al | Qt::AlignTop | Qt::TextDontClip), text->text);
    }
    image = mu::fromQImage(img);
}
