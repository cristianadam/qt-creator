// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <modelnode.h>
#include <nodeinstanceview.h>
#include <QRectF>
#include <qmlitemnode.h>

namespace QmlDesigner {

class NodeInstanceView;

class QMLDESIGNER_EXPORT QmlAnchorBindingProxy : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool topAnchored READ topAnchored WRITE setTopAnchor NOTIFY topAnchorChanged)
    Q_PROPERTY(bool bottomAnchored READ bottomAnchored WRITE setBottomAnchor NOTIFY bottomAnchorChanged)
    Q_PROPERTY(bool leftAnchored READ leftAnchored WRITE setLeftAnchor NOTIFY leftAnchorChanged)
    Q_PROPERTY(bool rightAnchored READ rightAnchored WRITE setRightAnchor NOTIFY rightAnchorChanged)
    Q_PROPERTY(bool hasParent READ hasParent NOTIFY parentChanged)
    Q_PROPERTY(bool isInLayout READ isInLayout NOTIFY parentChanged)

    Q_PROPERTY(QString topTarget READ topTarget WRITE setTopTarget NOTIFY topTargetChanged)
    Q_PROPERTY(QString bottomTarget READ bottomTarget WRITE setBottomTarget NOTIFY bottomTargetChanged)
    Q_PROPERTY(QString leftTarget READ leftTarget WRITE setLeftTarget NOTIFY leftTargetChanged)
    Q_PROPERTY(QString rightTarget READ rightTarget WRITE setRightTarget NOTIFY rightTargetChanged)

    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetTop READ relativeAnchorTargetTop WRITE setRelativeAnchorTargetTop NOTIFY relativeAnchorTargetTopChanged)
    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetBottom READ relativeAnchorTargetBottom WRITE setRelativeAnchorTargetBottom NOTIFY relativeAnchorTargetBottomChanged)
    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetLeft READ relativeAnchorTargetLeft WRITE setRelativeAnchorTargetLeft NOTIFY relativeAnchorTargetLeftChanged)
    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetRight READ relativeAnchorTargetRight WRITE setRelativeAnchorTargetRight NOTIFY relativeAnchorTargetRightChanged)

    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetVertical READ relativeAnchorTargetVertical WRITE setRelativeAnchorTargetVertical NOTIFY relativeAnchorTargetVerticalChanged)
    Q_PROPERTY(RelativeAnchorTarget relativeAnchorTargetHorizontal READ relativeAnchorTargetHorizontal WRITE setRelativeAnchorTargetHorizontal NOTIFY relativeAnchorTargetHorizontalChanged)

    Q_PROPERTY(QString verticalTarget READ verticalTarget WRITE setVerticalTarget NOTIFY verticalTargetChanged)
    Q_PROPERTY(QString horizontalTarget READ horizontalTarget WRITE setHorizontalTarget NOTIFY horizontalTargetChanged)

    Q_PROPERTY(bool hasAnchors READ hasAnchors NOTIFY anchorsChanged)
    Q_PROPERTY(bool isFilled READ isFilled NOTIFY anchorsChanged)

    Q_PROPERTY(bool horizontalCentered READ horizontalCentered WRITE setHorizontalCentered NOTIFY centeredHChanged)
    Q_PROPERTY(bool verticalCentered READ verticalCentered WRITE setVerticalCentered NOTIFY centeredVChanged)
    Q_PROPERTY(QVariant itemNode READ itemNode NOTIFY itemNodeChanged)
    Q_PROPERTY(QVariant itemModelNode READ itemModelNode NOTIFY itemNodeChanged)

    Q_PROPERTY(QStringList possibleTargetItems READ possibleTargetItems NOTIFY itemNodeChanged)

public:
    enum RelativeAnchorTarget {
        SameEdge = 0,
        Center = 1,
        OppositeEdge = 2
    };
    Q_ENUM(RelativeAnchorTarget)

    //only enable if node has parent

    QmlAnchorBindingProxy(QObject *parent = nullptr);
    ~QmlAnchorBindingProxy() override;

    void setup(const QmlItemNode &itemNode);
    void invalidate(const QmlItemNode &itemNode);

    bool topAnchored() const;
    bool bottomAnchored() const;
    bool leftAnchored() const;
    bool rightAnchored() const;

    bool hasParent() const;
    bool isFilled() const;

    bool isInLayout() const;

    void removeTopAnchor();
    void removeBottomAnchor();
    void removeLeftAnchor();
    void removeRightAnchor();
    bool hasAnchors() const;

    bool horizontalCentered();
    bool verticalCentered();
    QVariant itemNode() const { return QVariant::fromValue(m_qmlItemNode.modelNode().id()); }
    QVariant itemModelNode() const { return QVariant::fromValue(m_qmlItemNode.modelNode()); }

    QString topTarget() const;
    QString bottomTarget() const;
    QString leftTarget() const;
    QString rightTarget() const;

    RelativeAnchorTarget relativeAnchorTargetTop() const;
    RelativeAnchorTarget relativeAnchorTargetBottom() const;
    RelativeAnchorTarget relativeAnchorTargetLeft() const;
    RelativeAnchorTarget relativeAnchorTargetRight() const;

    RelativeAnchorTarget relativeAnchorTargetVertical() const;
    RelativeAnchorTarget relativeAnchorTargetHorizontal() const;

    QString verticalTarget() const;
    QString horizontalTarget() const;

    QmlItemNode getItemNode() const { return m_qmlItemNode; }

public:
    void setTopTarget(const QString &target);
    void setBottomTarget(const QString &target);
    void setLeftTarget(const QString &target);
    void setRightTarget(const QString &target);
    void setVerticalTarget(const QString &target);
    void setHorizontalTarget(const QString &target);

    void setRelativeAnchorTargetTop(RelativeAnchorTarget target);
    void setRelativeAnchorTargetBottom(RelativeAnchorTarget target);
    void setRelativeAnchorTargetLeft(RelativeAnchorTarget target);
    void setRelativeAnchorTargetRight(RelativeAnchorTarget target);

    void setRelativeAnchorTargetVertical(RelativeAnchorTarget target);
    void setRelativeAnchorTargetHorizontal(RelativeAnchorTarget target);

    QStringList possibleTargetItems() const;
    Q_INVOKABLE int indexOfPossibleTargetItem(const QString &targetName) const;

    static void registerDeclarativeType();

public slots:
    void resetLayout();
    void fill();
    void centerIn();
    void setTopAnchor(bool anchor =true);
    void setBottomAnchor(bool anchor = true);
    void setLeftAnchor(bool anchor = true);
    void setRightAnchor(bool anchor = true);

    void setVerticalCentered(bool centered = true);
    void setHorizontalCentered(bool centered = true);

signals:
    void parentChanged();

    void topAnchorChanged();
    void bottomAnchorChanged();
    void leftAnchorChanged();
    void rightAnchorChanged();
    void centeredVChanged();
    void centeredHChanged();
    void anchorsChanged();
    void itemNodeChanged();

    void topTargetChanged();
    void bottomTargetChanged();
    void leftTargetChanged();
    void rightTargetChanged();

    void verticalTargetChanged();
    void horizontalTargetChanged();

    void relativeAnchorTargetTopChanged();
    void relativeAnchorTargetBottomChanged();
    void relativeAnchorTargetLeftChanged();
    void relativeAnchorTargetRightChanged();

    void relativeAnchorTargetVerticalChanged();
    void relativeAnchorTargetHorizontalChanged();

    void invalidated();

private:
    void setDefaultAnchorTarget(const ModelNode &modelNode);
    void anchorTop();
    void anchorBottom();
    void anchorLeft();
    void anchorRight();

    void anchorVertical();
    void anchorHorizontal();

    void setupAnchorTargets();
    void emitAnchorSignals();

    void setDefaultRelativeTopTarget();
    void setDefaultRelativeBottomTarget();
    void setDefaultRelativeLeftTarget();
    void setDefaultRelativeRightTarget();

    bool executeInTransaction(const QByteArray &identifier, const AbstractView::OperationBlock &lambda);

    QmlItemNode targetIdToNode(const QString &id) const;
    QString idForNode(const QmlItemNode &qmlItemNode) const;

    ModelNode modelNode() const;

    QmlItemNode m_qmlItemNode;

    QRectF parentBoundingBox();

    QRectF boundingBox(const QmlItemNode &node);

    QRectF transformedBoundingBox();

    QmlItemNode m_topTarget;
    QmlItemNode m_bottomTarget;
    QmlItemNode m_leftTarget;
    QmlItemNode m_rightTarget;

    QmlItemNode m_verticalTarget;
    QmlItemNode m_horizontalTarget;

    RelativeAnchorTarget m_relativeTopTarget;
    RelativeAnchorTarget m_relativeBottomTarget;
    RelativeAnchorTarget m_relativeLeftTarget;
    RelativeAnchorTarget m_relativeRightTarget;

    RelativeAnchorTarget m_relativeVerticalTarget;
    RelativeAnchorTarget m_relativeHorizontalTarget;

    bool m_locked;
    bool m_ignoreQml;
};

} // namespace QmlDesigner
