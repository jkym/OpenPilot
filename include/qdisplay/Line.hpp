#include <QGraphicsLineItem>

namespace jafar {
namespace qdisplay {
class Viewer;
class Line : public QGraphicsLineItem {
  public:
    /**
      * Create an Line to use with a Viewer to display a basic shape.
      */
    Line(double x, double y, double w, double h);
    /**
     * Set the color of the line of the shape
     * @param r red [0 to 255]
     * @param g green [0 to 255]
     * @param b blue [0 to 255]
     */
    void setColor(int r, int g, int b);
    /**
     * Set the position of the shape
     * @param x
     * @param y
     */
    inline void setPos(double x, double y) { QGraphicsLineItem::setPos(x,y); }
};

}

}

