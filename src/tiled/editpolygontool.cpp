/*
 * editpolygontool.cpp
 * Copyright 2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "editpolygontool.h"

#include "addremovemapobject.h"
#include "changepolygon.h"
#include "changebezier.h"
#include "layer.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "mapscene.h"
#include "preferences.h"
#include "rangeset.h"
#include "selectionrectangle.h"
#include "utils.h"

#include <QApplication>
#include <QGraphicsItem>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QUndoStack>

using namespace Tiled;
using namespace Tiled::Internal;

namespace Tiled {
namespace Internal {

/**
 * A handle that allows moving around a point of a polygon.
 */
class PointHandle : public QGraphicsItem
{
public:
    PointHandle(MapObjectItem *mapObjectItem, int pointIndex)
        : QGraphicsItem()
        , mMapObjectItem(mapObjectItem)
        , mPointIndex(pointIndex)
        , mSelected(false)
    {
        setFlags(QGraphicsItem::ItemIgnoresTransformations |
                 QGraphicsItem::ItemIgnoresParentOpacity);
        setZValue(10000);
        setCursor(Qt::SizeAllCursor);
    }

    MapObjectItem *mapObjectItem() const { return mMapObjectItem; }
    MapObject *mapObject() const { return mMapObjectItem->mapObject(); }

    int pointIndex() const { return mPointIndex; }

    void setPointPosition(const QPointF &pos);

    // These hide the QGraphicsItem members
    void setSelected(bool selected) { mSelected = selected; update(); }
    bool isSelected() const { return mSelected; }

    QRectF boundingRect() const;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = 0);

private:
    MapObjectItem *mMapObjectItem;
    int mPointIndex;
    bool mSelected;
};

/**
 * A handle that allows moving around a point of a polygon.
 */
class ControlPointHandle : public QGraphicsItem
{
public:
    ControlPointHandle(MapObjectItem *mapObjectItem, int pointIndex, bool isRightControlPoint)
        : QGraphicsItem()
        , mMapObjectItem(mapObjectItem)
        , mPointIndex(pointIndex)
        , mIsRightControlPoint(isRightControlPoint)
    {
        setFlags(QGraphicsItem::ItemIgnoresTransformations |
                 QGraphicsItem::ItemIgnoresParentOpacity);
        setZValue(10000);
        setCursor(Qt::SizeAllCursor);
    }

    MapObjectItem *mapObjectItem() const { return mMapObjectItem; }
    MapObject *mapObject() const { return mMapObjectItem->mapObject(); }

    int pointIndex() const { return mPointIndex; }

    void setPointPosition(const QPointF &pos);

    QRectF boundingRect() const;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = 0);

private:
    MapObjectItem *mMapObjectItem;
    int mPointIndex;
    bool mIsRightControlPoint;
    QPointF mReferencePosition;
};

/**
 * A line that connects the bezier and control points handles
 * to indicate which control points belong to which polygon point
 */
class ControlPointConnector : public QGraphicsItem
{
public:
    ControlPointConnector(MapObjectItem *object, MapRenderer *renderer, int pointIndex, bool isRightControlPoint)
        : QGraphicsItem()
        , mMapObjectItem(object)
        , mRenderer(renderer)
        , mPointIndex(pointIndex)
        , mIsRightControlPoint(isRightControlPoint)

    {

        setFlags(QGraphicsItem::ItemIgnoresParentOpacity);
        setZValue(10000);
    }

    QRectF boundingRect() const;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = 0);

private:
    MapObjectItem *mMapObjectItem;
    MapRenderer *mRenderer;
    int mPointIndex;
    bool mIsRightControlPoint;
};

} // namespace Internal
} // namespace Tiled

void PointHandle::setPointPosition(const QPointF &pos)
{
    // TODO: It could be faster to update the polygon only once when changing
    // multiple points of the same polygon.
    MapObject *mapObject = mMapObjectItem->mapObject();
    QPolygonF polygon = mapObject->polygon();
    polygon[mPointIndex] = pos - mapObject->position();
    mMapObjectItem->setPolygon(polygon);
}

QRectF PointHandle::boundingRect() const
{
    return QRectF(-5, -5, 10 + 1, 10 + 1);
}

void PointHandle::paint(QPainter *painter,
                        const QStyleOptionGraphicsItem *,
                        QWidget *)
{
    painter->setPen(Qt::black);
    if (mSelected) {
        painter->setBrush(QApplication::palette().highlight());
        painter->drawRect(QRectF(-4, -4, 8, 8));
    } else {
        painter->setBrush(Qt::lightGray);
        painter->drawRect(QRectF(-3, -3, 6, 6));
    }
}

void ControlPointHandle::setPointPosition(const QPointF &pos)
{
    MapObject *mapObject = mMapObjectItem->mapObject();
    QPolygonF leftControlPoints = mapObject->leftControlPoints();
    QPolygonF rightControlPoints = mapObject->rightControlPoints();
    QPolygonF polygon = mapObject->polygon();

    QPolygonF &changedControlPoints = mIsRightControlPoint ?
                rightControlPoints : leftControlPoints;
    changedControlPoints[mPointIndex] = pos - mapObject->position();
    mapObjectItem()->setBezier(polygon, leftControlPoints, rightControlPoints);
}

QRectF ControlPointHandle::boundingRect() const
{
    return QRectF(-5, -5, 10 + 1, 10 + 1);
}

void ControlPointHandle::paint(QPainter *painter,
                        const QStyleOptionGraphicsItem *,
                        QWidget *)
{
    painter->setPen(Qt::black);
    painter->setBrush(Qt::black);
    painter->drawEllipse(QPointF(0,0), 3,3);
}

QRectF ControlPointConnector::boundingRect() const
{
    MapObject *mapObject = mMapObjectItem->mapObject();
    QPointF point = mapObject->polygon().at(mPointIndex);
    QPointF controlPoint = mIsRightControlPoint ?
                mapObject->rightControlPoints().at(mPointIndex) :
                mapObject->leftControlPoints().at(mPointIndex);

    const QPointF pointPos = mRenderer->pixelToScreenCoords(point);
    const QPointF pointScene = mMapObjectItem->mapToScene(pointPos);

    const QPointF controlPointPos = mRenderer->pixelToScreenCoords(controlPoint);
    const QPointF controlPointScene = mMapObjectItem->mapToScene(controlPointPos);

    return QRectF(pointScene, controlPointScene);
}

void ControlPointConnector::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    MapObject *mapObject = mMapObjectItem->mapObject();
    QPen pen;
    pen.setCosmetic(true);
    pen.setColor(Qt::black);
    painter->setPen(pen);
    QPointF point = mapObject->polygon().at(mPointIndex);
    QPointF controlPoint = mIsRightControlPoint ?
                mapObject->rightControlPoints().at(mPointIndex) :
                mapObject->leftControlPoints().at(mPointIndex);

    const QPointF pointPos = mRenderer->pixelToScreenCoords(point);
    const QPointF pointScene = mMapObjectItem->mapToScene(pointPos);

    const QPointF controlPointPos = mRenderer->pixelToScreenCoords(controlPoint);
    const QPointF controlPointScene = mMapObjectItem->mapToScene(controlPointPos);

    painter->drawLine(pointScene, controlPointScene) ;
}

EditPolygonTool::EditPolygonTool(QObject *parent)
    : AbstractObjectTool(tr("Edit Polygons"),
          QIcon(QLatin1String(":images/24x24/tool-edit-polygons.png")),
          QKeySequence(tr("E")),
          parent)
    , mSelectionRectangle(new SelectionRectangle)
    , mMousePressed(false)
    , mClickedHandle(0)
    , mClickedControlPointHandle(0)
    , mClickedObjectItem(0)
    , mMode(NoMode)
{
}

EditPolygonTool::~EditPolygonTool()
{
    delete mSelectionRectangle;
}

void EditPolygonTool::activate(MapScene *scene)
{
    AbstractObjectTool::activate(scene);

    updateHandles();

    // TODO: Could be more optimal by separating the updating of handles from
    // the creation and removal of handles depending on changes in the
    // selection, and by only updating the handles of the objects that changed.
    connect(mapDocument(), SIGNAL(objectsChanged(QList<MapObject*>)),
            this, SLOT(updateHandles()));
    connect(scene, SIGNAL(selectedObjectItemsChanged()),
            this, SLOT(updateHandles()));

    connect(mapDocument(), SIGNAL(objectsRemoved(QList<MapObject*>)),
            this, SLOT(objectsRemoved(QList<MapObject*>)));
}

void EditPolygonTool::deactivate(MapScene *scene)
{
    disconnect(mapDocument(), SIGNAL(objectsChanged(QList<MapObject*>)),
               this, SLOT(updateHandles()));
    disconnect(scene, SIGNAL(selectedObjectItemsChanged()),
               this, SLOT(updateHandles()));

    // Delete all handles
    QMapIterator<MapObjectItem*, QList<PointHandle*> > i(mHandles);
    while (i.hasNext())
        qDeleteAll(i.next().value());

    QMapIterator<MapObjectItem*, QList<ControlPointHandle*> > l(mLeftControlPointHandles);
    while (l.hasNext())
        qDeleteAll(l.next().value());
    QMapIterator<MapObjectItem*, QList<ControlPointHandle*> > r(mRightControlPointHandles);
    while (r.hasNext())
        qDeleteAll(r.next().value());

    QMapIterator<MapObjectItem*, QList<ControlPointConnector*> > c(mControlPointConnectors);
    while (c.hasNext())
        qDeleteAll(c.next().value());

    mHandles.clear();
    mLeftControlPointHandles.clear();
    mRightControlPointHandles.clear();
    mControlPointConnectors.clear();
    mSelectedHandles.clear();
    mClickedHandle = 0;
    mClickedControlPointHandle = 0;

    AbstractObjectTool::deactivate(scene);
}

void EditPolygonTool::mouseEntered()
{
}

void EditPolygonTool::mouseMoved(const QPointF &pos,
                                 Qt::KeyboardModifiers modifiers)
{
    AbstractObjectTool::mouseMoved(pos, modifiers);

    if (mMode == NoMode && mMousePressed) {
        QPoint screenPos = QCursor::pos();
        const int dragDistance = (mScreenStart - screenPos).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            //prioritize control points over polygon handles
            if (mClickedControlPointHandle) {
                startMovingControlPoint();
            }
            else if (mClickedHandle)
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
        mSelectionRectangle->setRectangle(QRectF(mStart, pos).normalized());
        break;
    case Moving:
        updateMovingItems(pos, modifiers);
        break;
    case MovingControlPoint:
        updateMovingControlPoint(pos, modifiers);
    case NoMode:
        break;
    }
}

template <class T>
static T *first(const QList<QGraphicsItem *> &items)
{
    foreach (QGraphicsItem *item, items) {
        if (T *t = dynamic_cast<T*>(item))
            return t;
    }
    return 0;
}

void EditPolygonTool::mousePressed(QGraphicsSceneMouseEvent *event)
{
    if (mMode != NoMode) // Ignore additional presses during select/move
        return;

    switch (event->button()) {
    case Qt::LeftButton: {
        mMousePressed = true;
        mStart = event->scenePos();
        mScreenStart = event->screenPos();

        const QList<QGraphicsItem *> items = mapScene()->items(mStart);
        mClickedObjectItem = first<MapObjectItem>(items);
        mClickedHandle = first<PointHandle>(items);

        mClickedControlPointHandle = first<ControlPointHandle>(items);
        break;
    }
    case Qt::RightButton: {
        QList<QGraphicsItem *> items = mapScene()->items(event->scenePos());
        PointHandle *clickedHandle = first<PointHandle>(items);
        if (clickedHandle || !mSelectedHandles.isEmpty()) {
            showHandleContextMenu(clickedHandle,
                                  event->screenPos());
        } else {
            AbstractObjectTool::mousePressed(event);
        }
        break;
    }
    default:
        AbstractObjectTool::mousePressed(event);
        break;
    }
}

void EditPolygonTool::mouseReleased(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
        if (mClickedHandle) {
            QSet<PointHandle*> selection = mSelectedHandles;
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
                if (selection.contains(mClickedHandle))
                    selection.remove(mClickedHandle);
                else
                    selection.insert(mClickedHandle);
            } else {
                selection.clear();
                selection.insert(mClickedHandle);
            }
            setSelectedHandles(selection);
        } else if (mClickedObjectItem) {
            QSet<MapObjectItem*> selection = mapScene()->selectedObjectItems();
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
                if (selection.contains(mClickedObjectItem))
                    selection.remove(mClickedObjectItem);
                else
                    selection.insert(mClickedObjectItem);
            } else {
                selection.clear();
                selection.insert(mClickedObjectItem);
            }
            mapScene()->setSelectedObjectItems(selection);
            updateHandles();
        } else if (!mSelectedHandles.isEmpty()) {
            // First clear the handle selection
            setSelectedHandles(QSet<PointHandle*>());
        } else {
            // If there is no handle selection, clear the object selection
            mapScene()->setSelectedObjectItems(QSet<MapObjectItem*>());
            updateHandles();
        }
        break;
    case Selecting:
        updateSelection(event->scenePos(), event->modifiers());
        mapScene()->removeItem(mSelectionRectangle);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case MovingControlPoint:
        finishMovingControlPoint(event->scenePos());
        break;
    }

    mMousePressed = false;
    mClickedHandle = 0;
}

void EditPolygonTool::modifiersChanged(Qt::KeyboardModifiers modifiers)
{
    mModifiers = modifiers;
}

void EditPolygonTool::languageChanged()
{
    setName(tr("Edit Polygons"));
    setShortcut(QKeySequence(tr("E")));
}

void EditPolygonTool::setSelectedHandles(const QSet<PointHandle *> &handles)
{
    foreach (PointHandle *handle, mSelectedHandles)
        if (!handles.contains(handle))
            handle->setSelected(false);

    foreach (PointHandle *handle, handles)
        if (!mSelectedHandles.contains(handle))
            handle->setSelected(true);

    mSelectedHandles = handles;
}

/**
 * Creates and removes handle instances as necessary to adapt to a new object
 * selection.
 */
void EditPolygonTool::updateHandles()
{
    const QSet<MapObjectItem*> &selection = mapScene()->selectedObjectItems();

    // First destroy the handles for objects that are no longer selected
    QMutableMapIterator<MapObjectItem*, QList<PointHandle*> > i(mHandles);
    while (i.hasNext()) {
        i.next();
        if (!selection.contains(i.key())) {
            foreach (PointHandle *handle, i.value()) {
                if (handle->isSelected())
                    mSelectedHandles.remove(handle);
                delete handle;
            }

            i.remove();
        }
    }

    QMutableMapIterator<MapObjectItem*, QList<ControlPointHandle*> > l(mLeftControlPointHandles);
    while (l.hasNext()) {
        l.next();
        if (!selection.contains(l.key())) {
            foreach (ControlPointHandle *handle, l.value()) {
                delete handle;
            }
            l.remove();
        }
    }

    QMutableMapIterator<MapObjectItem*, QList<ControlPointHandle*> > r(mRightControlPointHandles);
    while (r.hasNext()) {
        r.next();
        if (!selection.contains(r.key())) {
            foreach (ControlPointHandle *handle, r.value()) {
                delete handle;
            }
            r.remove();
        }
    }

    QMutableMapIterator<MapObjectItem*, QList<ControlPointConnector*> > c(mControlPointConnectors);
    while (c.hasNext()) {
        c.next();
        if (!selection.contains(c.key())) {
            foreach (ControlPointConnector *handle, c.value()) {
                delete handle;
            }
            c.remove();
        }
    }

    MapRenderer *renderer = mapDocument()->renderer();

    foreach (MapObjectItem *item, selection) {
        const MapObject *object = item->mapObject();
        if (!object->cell().isEmpty())
            continue;

        QPolygonF polygon = object->polygon();
        polygon.translate(object->position());

        QPolygonF leftControlPoints = object->leftControlPoints();
        leftControlPoints.translate(object->position());

        QPolygonF rightControlPoints = object->rightControlPoints();
        rightControlPoints.translate(object->position());


        QList<PointHandle*> pointHandles = mHandles.value(item);
        QList<ControlPointHandle*> leftControlPointHandles = mLeftControlPointHandles.value(item);
        QList<ControlPointHandle*> rightControlPointHandles = mRightControlPointHandles.value(item);
        QList<ControlPointConnector*> controlPointConnectors = mControlPointConnectors.value(item);

        // Create missing handles
        while (pointHandles.size() < polygon.size()) {
            PointHandle *handle = new PointHandle(item, pointHandles.size());
            pointHandles.append(handle);
            mapScene()->addItem(handle);

            if(item->mapObject()->shape() == MapObject::Bezierline || item->mapObject()->shape() == MapObject::Bezierloop) {
                ControlPointHandle *leftControPointHandle = new ControlPointHandle(item, leftControlPointHandles.size(), false);
                ControlPointHandle *rightControPointHandle = new ControlPointHandle(item, rightControlPointHandles.size(), true);
                ControlPointConnector *leftConnection = new ControlPointConnector(item, renderer, leftControlPointHandles.size(), false);
                ControlPointConnector *rightConnection = new ControlPointConnector(item, renderer, rightControlPointHandles.size(), true);


                rightControlPointHandles.append(rightControPointHandle);
                leftControlPointHandles.append(leftControPointHandle);
                controlPointConnectors.append(leftConnection);
                controlPointConnectors.append(rightConnection);

                mapScene()->addItem(leftConnection);
                mapScene()->addItem(rightConnection);
                mapScene()->addItem(leftControPointHandle);
                mapScene()->addItem(rightControPointHandle);
            }
        }

        // Remove superfluous handles
        while (pointHandles.size() > polygon.size()) {
            PointHandle *handle = pointHandles.takeLast();
            if (handle->isSelected())
                mSelectedHandles.remove(handle);
            delete handle;

            //Number of polygon points is always the same as the number of controlpoints
            if (item->mapObject()->shape() == MapObject::Bezierline || item->mapObject()->shape()== MapObject::Bezierloop) {
                ControlPointHandle *leftControlPointHandle = leftControlPointHandles.takeLast();
                ControlPointHandle *rightControlPointHandle = rightControlPointHandles.takeLast();
                ControlPointConnector *firstConnector = controlPointConnectors.takeLast();
                ControlPointConnector *secondConnector = controlPointConnectors.takeLast();

                delete leftControlPointHandle;
                delete rightControlPointHandle;
                delete firstConnector;
                delete secondConnector;
            }
        }

        // Update the position of all handles
        for (int i = 0; i < pointHandles.size(); ++i) {
            const QPointF &point = polygon.at(i);
            const QPointF handlePos = renderer->pixelToScreenCoords(point);
            const QPointF internalHandlePos = handlePos - item->pos();
            pointHandles.at(i)->setPos(item->mapToScene(internalHandlePos));

            if (item->mapObject()->shape() == MapObject::Bezierline || item->mapObject()->shape() == MapObject::Bezierloop) {
                const QPointF &leftControlPoint = leftControlPoints.at(i);
                const QPointF leftControlPointHandlePos = renderer->pixelToScreenCoords(leftControlPoint);
                const QPointF internalLeftControlPointHandlePos = leftControlPointHandlePos - item->pos();
                QPointF internalLeftControlPointHandlePositionPixel = item->mapToScene(internalLeftControlPointHandlePos);
                leftControlPointHandles.at(i)->setPos(internalLeftControlPointHandlePositionPixel);

                const QPointF &rightControlPoint = rightControlPoints.at(i);
                const QPointF rightControlPointHandlePos = renderer->pixelToScreenCoords(rightControlPoint);
                const QPointF internalRighControlPointHandlePos = rightControlPointHandlePos - item->pos();
                QPointF internalRightControlPointHandlePositionPixel = item->mapToScene(internalRighControlPointHandlePos);
                rightControlPointHandles.at(i)->setPos(internalRightControlPointHandlePositionPixel);
            }
        }

        mHandles.insert(item, pointHandles);
        mLeftControlPointHandles.insert(item, leftControlPointHandles);
        mRightControlPointHandles.insert(item, rightControlPointHandles);
        mControlPointConnectors.insert(item, controlPointConnectors);
    }
}

void EditPolygonTool::objectsRemoved(const QList<MapObject *> &objects)
{
    if (mMode == Moving) {
        // Make sure we're not going to try to still change these objects when
        // finishing the move operation.
        // TODO: In addition to avoiding crashes, it would also be good to
        // disallow other actions while moving.
        foreach (MapObject *object, objects)
            mOldPolygons.remove(object);
    }
}

void EditPolygonTool::updateSelection(const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers)
{
    QRectF rect = QRectF(mStart, pos).normalized();

    // Make sure the rect has some contents, otherwise intersects returns false
    rect.setWidth(qMax(qreal(1), rect.width()));
    rect.setHeight(qMax(qreal(1), rect.height()));

    const QSet<MapObjectItem*> oldSelection = mapScene()->selectedObjectItems();

    if (oldSelection.isEmpty()) {
        // Allow selecting some map objects only when there aren't any selected
        QSet<MapObjectItem*> selectedItems;

        foreach (QGraphicsItem *item, mapScene()->items(rect)) {
            MapObjectItem *mapObjectItem = dynamic_cast<MapObjectItem*>(item);
            if (mapObjectItem)
                selectedItems.insert(mapObjectItem);
        }


        QSet<MapObjectItem*> newSelection;

        if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) {
            newSelection = oldSelection | selectedItems;
        } else {
            newSelection = selectedItems;
        }

        mapScene()->setSelectedObjectItems(newSelection);
        updateHandles();
    } else {
        // Update the selected handles
        QSet<PointHandle*> selectedHandles;

        foreach (QGraphicsItem *item, mapScene()->items(rect)) {
            if (PointHandle *handle = dynamic_cast<PointHandle*>(item))
                selectedHandles.insert(handle);
        }

        if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier))
            setSelectedHandles(mSelectedHandles | selectedHandles);
        else
            setSelectedHandles(selectedHandles);
    }
}

void EditPolygonTool::startSelecting()
{
    mMode = Selecting;
    mapScene()->addItem(mSelectionRectangle);
}

void EditPolygonTool::startMoving()
{
    // Move only the clicked handle, if it was not part of the selection
    if (!mSelectedHandles.contains(mClickedHandle))
        setSelectedHandle(mClickedHandle);

    mMode = Moving;

    MapRenderer *renderer = mapDocument()->renderer();

    // Remember the current object positions
    mOldHandlePositions.clear();
    mOldPolygons.clear();
    mAlignPosition = renderer->screenToPixelCoords((*mSelectedHandles.begin())->pos());

    foreach (PointHandle *handle, mSelectedHandles) {
        const QPointF pos = renderer->screenToPixelCoords(handle->pos());
        mOldHandlePositions.append(handle->pos());
        if (pos.x() < mAlignPosition.x())
            mAlignPosition.setX(pos.x());
        if (pos.y() < mAlignPosition.y())
            mAlignPosition.setY(pos.y());

        MapObject *mapObject = handle->mapObject();
        if (!mOldPolygons.contains(mapObject))
            mOldPolygons.insert(mapObject, mapObject->polygon());
    }
}

void EditPolygonTool::startMovingControlPoint()
{
    mMode = MovingControlPoint;

    mOldLeftControlPoints.clear();
    mOldRightControlPoints.clear();

    mOldLeftControlPoints = mClickedControlPointHandle->mapObject()->leftControlPoints();
    mOldRightControlPoints = mClickedControlPointHandle->mapObject()->rightControlPoints();
}

void EditPolygonTool::updateMovingItems(const QPointF &pos,
                                        Qt::KeyboardModifiers modifiers)
{
    MapRenderer *renderer = mapDocument()->renderer();
    QPointF diff = pos - mStart;

    bool snapToGrid = Preferences::instance()->snapToGrid();
    bool snapToFineGrid = Preferences::instance()->snapToFineGrid();
    if (modifiers & Qt::ControlModifier) {
        snapToGrid = !snapToGrid;
        snapToFineGrid = false;
    }

    if (snapToGrid || snapToFineGrid) {
        int scale = snapToFineGrid ? Preferences::instance()->gridFine() : 1;
        const QPointF alignScreenPos =
                renderer->pixelToScreenCoords(mAlignPosition);
        const QPointF newAlignPixelPos = alignScreenPos + diff;

        // Snap the position to the grid
        QPointF newTileCoords =
                (renderer->screenToTileCoords(newAlignPixelPos) * scale).toPoint();
        newTileCoords /= scale;
        diff = renderer->tileToScreenCoords(newTileCoords) - alignScreenPos;
    }

    int i = 0;
    foreach (PointHandle *handle, mSelectedHandles) {
        MapObjectItem *item = handle->mapObjectItem();
        MapObject *object = item->mapObject();
        const QPointF newPixelPos = mOldHandlePositions.at(i) + diff;
        const QPointF newInternalPos = item->mapFromScene(newPixelPos);
        const QPointF newScenePos = item->pos() + newInternalPos;
        handle->setPos(newPixelPos);

        int pointIndex = handle->pointIndex();
        const QPointF oldPolygonPosition = object->polygon().at(pointIndex);
        const QPointF newPolygonPosition = renderer->screenToPixelCoords(newScenePos);
        handle->setPointPosition(newPolygonPosition);

        if (object->shape() == MapObject::Bezierline || object->shape() == MapObject::Bezierloop) {
            ControlPointHandle *leftControlPointHandle = mLeftControlPointHandles.value(item).at(pointIndex);
            ControlPointHandle *rightControlPointHandle = mRightControlPointHandles.value(item).at(pointIndex);

           const QPointF delta = newPolygonPosition - oldPolygonPosition;
           const QPointF oldLeftControlPoint = object->leftControlPoints().at(pointIndex);
           const QPointF oldRightControlPoint = object->rightControlPoints().at(pointIndex);

           QPointF newLeftControlPoint = oldLeftControlPoint + delta;
           const QPointF leftControlPointScreen = renderer->pixelToScreenCoords(newLeftControlPoint - object->position());
           const QPointF leftControlPointScene = item->mapToScene(leftControlPointScreen);

           QPointF newRightControlPoint = oldRightControlPoint + delta;
           const QPointF rightControlPointScreen = renderer->pixelToScreenCoords(newRightControlPoint - object->position());
           const QPointF rightControlPointScene = item->mapToScene(rightControlPointScreen);


           leftControlPointHandle->setPointPosition(newLeftControlPoint);
           leftControlPointHandle->setPos(leftControlPointScene);
           rightControlPointHandle->setPointPosition(newRightControlPoint);
           rightControlPointHandle->setPos(rightControlPointScene);
        }
        ++i;
    }
}

void EditPolygonTool::updateMovingControlPoint(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    QPointF newPosition = pos;

    MapRenderer *renderer = mapDocument()->renderer();
    bool snapToGrid = Preferences::instance()->snapToGrid();
    bool snapToFineGrid = Preferences::instance()->snapToFineGrid();
    if (modifiers & Qt::ControlModifier) {
        snapToGrid = !snapToGrid;
        snapToFineGrid = false;
    }

    if (snapToGrid || snapToFineGrid) {
        int scale = snapToFineGrid ? Preferences::instance()->gridFine() : 1;
        // Snap the position to the grid
        QPointF newTileCoords =
                (renderer->screenToTileCoords(newPosition) * scale).toPoint();
        newTileCoords /= scale;
        newPosition = renderer->tileToScreenCoords(newTileCoords);
    }

    const MapObjectItem *item = mClickedControlPointHandle->mapObjectItem();
    const QPointF newInternalPos = item->mapFromScene(newPosition);
    const QPointF newScenePos = item->pos() + newInternalPos;

    mClickedControlPointHandle->setPos(newPosition);
    mClickedControlPointHandle->setPointPosition(renderer->screenToPixelCoords(newScenePos));
}

void EditPolygonTool::finishMoving(const QPointF &pos)
{
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    if (mStart == pos || mOldPolygons.isEmpty()) // Move is a no-op
        return;

    QUndoStack *undoStack = mapDocument()->undoStack();
    undoStack->beginMacro(tr("Move %n Point(s)", "", mSelectedHandles.size()));

    // TODO: This isn't really optimal. Would be better to have a single undo
    // command that supports changing multiple map objects.
    QMapIterator<MapObject*, QPolygonF> i(mOldPolygons);
    while (i.hasNext()) {
        i.next();
        undoStack->push(new ChangePolygon(mapDocument(), i.key(), i.value()));
    }

    undoStack->endMacro();

    mOldHandlePositions.clear();
    mOldPolygons.clear();
}

void EditPolygonTool::finishMovingControlPoint(const QPointF &pos)
{
    Q_ASSERT(mMode == MovingControlPoint);
    mMode = NoMode;

    if (mStart == pos)
        return;

    MapObject *changedObject = mClickedControlPointHandle->mapObject();
    QUndoStack *undoStack = mapDocument()->undoStack();
    undoStack->beginMacro(tr("Move Control Point", ""));
    undoStack->push(new ChangeBezier(mapDocument(),
                                     changedObject,
                                     changedObject->polygon(),
                                     mOldLeftControlPoints,
                                     mOldRightControlPoints));
    undoStack->endMacro();

    mOldLeftControlPoints.clear();
    mOldRightControlPoints.clear();
    mClickedControlPointHandle = 0;
}

void EditPolygonTool::showHandleContextMenu(PointHandle *clickedHandle,
                                            QPoint screenPos)
{
    if (clickedHandle && !mSelectedHandles.contains(clickedHandle))
        setSelectedHandle(clickedHandle);

    const int n = mSelectedHandles.size();
    Q_ASSERT(n > 0);

    QIcon delIcon(QLatin1String(":images/16x16/edit-delete.png"));
    QString delText = tr("Delete %n Node(s)", "", n);

    QMenu menu;

    QAction *deleteNodesAction = menu.addAction(delIcon, delText);
    QAction *joinNodesAction = menu.addAction(tr("Join Nodes"));
    QAction *splitSegmentsAction = menu.addAction(tr("Split Segments"));

    Utils::setThemeIcon(deleteNodesAction, "edit-delete");

    joinNodesAction->setEnabled(n > 1);
    splitSegmentsAction->setEnabled(n > 1);

    connect(deleteNodesAction, SIGNAL(triggered()), SLOT(deleteNodes()));
    connect(joinNodesAction, SIGNAL(triggered()), SLOT(joinNodes()));
    connect(splitSegmentsAction, SIGNAL(triggered()), SLOT(splitSegments()));

    menu.exec(screenPos);
}

typedef QMap<MapObject*, RangeSet<int> > PointIndexesByObject;
static PointIndexesByObject
groupIndexesByObject(const QSet<PointHandle*> &handles)
{
    PointIndexesByObject result;

    // Build the list of point indexes for each map object
    foreach (PointHandle *handle, handles) {
        RangeSet<int> &pointIndexes = result[handle->mapObject()];
        pointIndexes.insert(handle->pointIndex());
    }

    return result;
}

void EditPolygonTool::deleteNodes()
{
    if (mSelectedHandles.isEmpty())
        return;

    PointIndexesByObject p = groupIndexesByObject(mSelectedHandles);
    QMapIterator<MapObject*, RangeSet<int> > i(p);

    QUndoStack *undoStack = mapDocument()->undoStack();

    QString delText = tr("Delete %n Node(s)", "", mSelectedHandles.size());
    undoStack->beginMacro(delText);

    while (i.hasNext()) {
        MapObject *object = i.next().key();
        const RangeSet<int> &indexRanges = i.value();

        QPolygonF oldPolygon = object->polygon();
        QPolygonF newPolygon = oldPolygon;

        QPolygonF oldLeftControlPoints = object->leftControlPoints();
        QPolygonF newLeftControlPoints = oldLeftControlPoints;

        QPolygonF oldRightControlPoints = object->rightControlPoints();
        QPolygonF newRightControlPoints = oldRightControlPoints;

        // Remove points, back to front to keep the indexes valid
        RangeSet<int>::Range it = indexRanges.end();
        RangeSet<int>::Range begin = indexRanges.begin();
        // assert: end != begin, since there is at least one entry
        do {
            --it;
            newPolygon.remove(it.first(), it.length());
            if (object->shape() == MapObject::Bezierline || object->shape() == MapObject::Bezierloop) {
                newLeftControlPoints.remove(it.first(), it.length());
                newRightControlPoints.remove(it.first(), it.length());
            }
        } while (it != begin);

        if (newPolygon.size() < 2) {
            // We've removed the entire object
            undoStack->push(new RemoveMapObject(mapDocument(), object));
        } else {
            if (object->shape() == MapObject::Bezierline || object->shape() == MapObject::Bezierloop) {
                object->setPolygon(newPolygon);
                object->setLeftControlPoints(newLeftControlPoints);
                object->setRightControlPoints(newRightControlPoints);
                undoStack->push(new ChangeBezier(mapDocument(), object,
                                              oldPolygon, oldLeftControlPoints, oldRightControlPoints));
            } else {
                object->setPolygon(newPolygon);
                undoStack->push(new ChangePolygon(mapDocument(), object,
                                              oldPolygon));
            }
        }
    }

    undoStack->endMacro();
}

/**
 * Joins the nodes at the given \a indexRanges. Each consecutive sequence
 * of nodes will be joined into a single node at the average location.
 *
 * This method can deal with both polygons as well as polylines. For polygons,
 * pass <code>true</code> for \a closed.
 */
static QPolygonF joinPolygonNodes(const QPolygonF &polygon,
                                  const RangeSet<int> &indexRanges,
                                  bool closed)
{
    if (indexRanges.isEmpty())
        return polygon;

    // Do nothing when dealing with a polygon with less than 3 points
    // (we'd no longer have a polygon)
    const int n = polygon.size();
    if (n < 3)
        return polygon;

    RangeSet<int>::Range firstRange = indexRanges.begin();
    RangeSet<int>::Range it = indexRanges.end();

    RangeSet<int>::Range lastRange = it;
    --lastRange; // We know there is at least one range

    QPolygonF result = polygon;

    // Indexes need to be offset when first and last range are joined.
    int indexOffset = 0;

    // Check whether the first and last ranges connect
    if (firstRange.first() == 0 && lastRange.last() == n - 1) {
        // Do nothing when the selection spans the whole polygon
        if (firstRange == lastRange)
            return polygon;

        // Join points of the first and last range when the polygon is closed
        if (closed) {
            QPointF averagePoint;
            for (int i = firstRange.first(); i <= firstRange.last(); i++)
                averagePoint += polygon.at(i);
            for (int i = lastRange.first(); i <= lastRange.last(); i++)
                averagePoint += polygon.at(i);
            averagePoint /= firstRange.length() + lastRange.length();

            result.remove(lastRange.first(), lastRange.length());
            result.remove(1, firstRange.length() - 1);
            result.replace(0, averagePoint);

            indexOffset = firstRange.length() - 1;

            // We have dealt with these ranges now
            // assert: firstRange != lastRange
            ++firstRange;
            --it;
        }
    }

    while (it != firstRange) {
        --it;

        // Merge the consecutive nodes into a single average point
        QPointF averagePoint;
        for (int i = it.first(); i <= it.last(); i++)
            averagePoint += polygon.at(i - indexOffset);
        averagePoint /= it.length();

        result.remove(it.first() + 1 - indexOffset, it.length() - 1);
        result.replace(it.first() - indexOffset, averagePoint);
    }

    return result;
}

/**
 * Splits the selected segments by inserting new nodes in the middle. The
 * selected segments are defined by each pair of consecutive \a indexRanges.
 *
 * This method can deal with both polygons as well as polylines. For polygons,
 * pass <code>true</code> for \a closed.
 */
static QPolygonF splitPolygonSegments(const QPolygonF &polygon,
                                      const RangeSet<int> &indexRanges,
                                      bool closed)
{
    if (indexRanges.isEmpty())
        return polygon;

    const int n = polygon.size();

    QPolygonF result = polygon;

    RangeSet<int>::Range firstRange = indexRanges.begin();
    RangeSet<int>::Range it = indexRanges.end();
    // assert: firstRange != it

    if (closed) {
        RangeSet<int>::Range lastRange = it;
        --lastRange; // We know there is at least one range

        // Handle the case where the first and last nodes are selected
        if (firstRange.first() == 0 && lastRange.last() == n - 1) {
            const QPointF splitPoint = (result.first() + result.last()) / 2;
            result.append(splitPoint);
        }
    }

    do {
        --it;

        for (int i = it.last(); i > it.first(); --i) {
            const QPointF splitPoint = (result.at(i) + result.at(i - 1)) / 2;
            result.insert(i, splitPoint);
        }
    } while (it != firstRange);

    return result;
}


void EditPolygonTool::joinNodes()
{
    if (mSelectedHandles.size() < 2)
        return;

    const PointIndexesByObject p = groupIndexesByObject(mSelectedHandles);
    QMapIterator<MapObject*, RangeSet<int> > i(p);

    QUndoStack *undoStack = mapDocument()->undoStack();
    bool macroStarted = false;

    while (i.hasNext()) {
        MapObject *object = i.next().key();
        const RangeSet<int> &indexRanges = i.value();

        const bool closed = object->shape() == MapObject::Polygon ||
                            object->shape() == MapObject::Bezierloop;

        QPolygonF oldPolygon = object->polygon();
        QPolygonF newPolygon = joinPolygonNodes(oldPolygon, indexRanges,
                                                closed);

        if (newPolygon.size() < oldPolygon.size()) {
            if (!macroStarted) {
                undoStack->beginMacro(tr("Join Nodes"));
                macroStarted = true;
            }

            //TODO: Better interpolation method for joining beziers
            if (object->shape() == MapObject::Bezierline || object->shape() == MapObject::Bezierloop) {
                QPolygonF oldLeftControlPoints = object->leftControlPoints();
                QPolygonF newLeftControlPoints = joinPolygonNodes(oldLeftControlPoints, indexRanges, closed);
                QPolygonF oldRightControlPoints = object->rightControlPoints();
                QPolygonF newRightControlPoints = joinPolygonNodes(oldRightControlPoints, indexRanges, closed);


                object->setPolygon(newPolygon);
                object->setLeftControlPoints(newLeftControlPoints);
                object->setRightControlPoints(newRightControlPoints);
                undoStack->push(new ChangeBezier(mapDocument(), object,
                                              oldPolygon, oldLeftControlPoints, oldRightControlPoints));

            } else {
                object->setPolygon(newPolygon);
                undoStack->push(new ChangePolygon(mapDocument(), object,
                                              oldPolygon));
            }
        }
    }

    if (macroStarted)
        undoStack->endMacro();
}

void EditPolygonTool::splitSegments()
{
    if (mSelectedHandles.size() < 2)
        return;

    const PointIndexesByObject p = groupIndexesByObject(mSelectedHandles);
    QMapIterator<MapObject*, RangeSet<int> > i(p);

    QUndoStack *undoStack = mapDocument()->undoStack();
    bool macroStarted = false;

    while (i.hasNext()) {
        MapObject *object = i.next().key();
        const RangeSet<int> &indexRanges = i.value();

        const bool closed = (object->shape() == MapObject::Polygon) || (object->shape() == MapObject::Bezierloop);
        QPolygonF oldPolygon = object->polygon();
        QPolygonF newPolygon = splitPolygonSegments(oldPolygon, indexRanges,
                                                    closed);

        if (newPolygon.size() > oldPolygon.size()) {
            if (!macroStarted) {
                undoStack->beginMacro(tr("Split Segments"));
                macroStarted = true;
            }

            //TODO: Better interpolation method for splitting beziers
            if (object->shape() == MapObject::Bezierline || object->shape() == MapObject::Bezierline) {
                QPolygonF oldLeftControlPoints = object->leftControlPoints();
                QPolygonF newLeftControlPoints = splitPolygonSegments(oldLeftControlPoints, indexRanges, closed);
                QPolygonF oldRightControlPoints = object->rightControlPoints();
                QPolygonF newRightControlPoints = splitPolygonSegments(oldRightControlPoints, indexRanges, closed);


                object->setPolygon(newPolygon);
                object->setLeftControlPoints(newLeftControlPoints);
                object->setRightControlPoints(newRightControlPoints);
                undoStack->push(new ChangeBezier(mapDocument(), object,
                                              oldPolygon, oldLeftControlPoints, oldRightControlPoints));

            } else {
                object->setPolygon(newPolygon);
                undoStack->push(new ChangePolygon(mapDocument(), object,
                                              oldPolygon));
            }
        }
    }

    if (macroStarted)
        undoStack->endMacro();
}
