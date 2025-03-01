/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "rewriterview.h"

#include "texttomodelmerger.h"
#include "modeltotextmerger.h"

#include <variantproperty.h>
#include <bindingproperty.h>
#include <rewritingexception.h>
#include <signalhandlerproperty.h>
#include <filemanager/astobjecttextextractor.h>
#include <filemanager/objectlengthcalculator.h>
#include <filemanager/firstdefinitionfinder.h>
#include <customnotifications.h>
#include <modelnodepositionstorage.h>
#include <modelnode.h>
#include <nodeproperty.h>
#include <qmlobjectnode.h>
#include <qmltimelinekeyframegroup.h>

#ifndef QMLDESIGNER_TEST
#include <qmldesignerplugin.h>
#include <viewmanager.h>
#endif

#include <qmljs/parser/qmljsengine_p.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/qmljssimplereader.h>

#include <utils/algorithm.h>
#include <utils/changeset.h>
#include <utils/qtcassert.h>

#include <QRegularExpression>

#include <utility>
#include <vector>
#include <algorithm>

using namespace QmlDesigner::Internal;

namespace QmlDesigner {

const char annotationsEscapeSequence[] = "##^##";

bool debugQmlPuppet()
{
#ifndef QMLDESIGNER_TEST
    if (!QmlDesignerPlugin::instance())
        return false;
    const QString debugPuppet = QmlDesignerPlugin::instance()->settings().value(DesignerSettingsKey::
        DEBUG_PUPPET).toString();
    return !debugPuppet.isEmpty();
#else
    return false;
#endif
}

RewriterView::RewriterView(DifferenceHandling differenceHandling, QObject *parent):
        AbstractView(parent),
        m_differenceHandling(differenceHandling),
        m_positionStorage(new ModelNodePositionStorage),
        m_modelToTextMerger(new Internal::ModelToTextMerger(this)),
        m_textToModelMerger(new Internal::TextToModelMerger(this))
{
    m_amendTimer.setSingleShot(true);
    m_amendTimer.setInterval(800);
    connect(&m_amendTimer, &QTimer::timeout, this, &RewriterView::amendQmlText);

    QmlJS::ModelManagerInterface *modelManager = QmlJS::ModelManagerInterface::instance();
    connect(modelManager, &QmlJS::ModelManagerInterface::libraryInfoUpdated,
            this, &RewriterView::handleLibraryInfoUpdate, Qt::QueuedConnection);
    connect(modelManager, &QmlJS::ModelManagerInterface::projectInfoUpdated,
            this, &RewriterView::handleProjectUpdate, Qt::DirectConnection);
    connect(this, &RewriterView::modelInterfaceProjectUpdated,
            this, &RewriterView::handleLibraryInfoUpdate, Qt::QueuedConnection);
}

RewriterView::~RewriterView() = default;

Internal::ModelToTextMerger *RewriterView::modelToTextMerger() const
{
    return m_modelToTextMerger.data();
}

Internal::TextToModelMerger *RewriterView::textToModelMerger() const
{
    return m_textToModelMerger.data();
}

void RewriterView::modelAttached(Model *model)
{
    m_modelAttachPending = false;

    if (model && model->textModifier())
        setTextModifier(model->textModifier());

    AbstractView::modelAttached(model);

    ModelAmender differenceHandler(m_textToModelMerger.data());
    const QString qmlSource = m_textModifier->text();
    if (m_textToModelMerger->load(qmlSource, differenceHandler))
        m_lastCorrectQmlSource = qmlSource;

    if (!(m_errors.isEmpty() && m_warnings.isEmpty()))
        notifyErrorsAndWarnings(m_errors);

    if (hasIncompleteTypeInformation()) {
        m_modelAttachPending = true;
        QTimer::singleShot(1000, this, [this, model](){
            modelAttached(model);
        });
    }
}

void RewriterView::modelAboutToBeDetached(Model * /*model*/)
{
    m_positionStorage->clear();
}

void RewriterView::nodeCreated(const ModelNode &createdNode)
{
    Q_ASSERT(textModifier());
    m_positionStorage->setNodeOffset(createdNode, ModelNodePositionStorage::INVALID_LOCATION);
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeCreated(createdNode);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeRemoved(const ModelNode &removedNode, const NodeAbstractProperty &parentProperty, PropertyChangeFlags propertyChange)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeRemoved(removedNode, parentProperty, propertyChange);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::propertiesAboutToBeRemoved(const QList<AbstractProperty> &propertyList)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    for (const AbstractProperty &property : propertyList) {
        if (!property.isDefaultProperty())
            continue;

        if (!m_removeDefaultPropertyTransaction.isValid()) {
            m_removeDefaultPropertyTransaction = beginRewriterTransaction(
                        QByteArrayLiteral("RewriterView::propertiesAboutToBeRemoved"));
        }

        if (property.isNodeListProperty()) {
            const auto nodeList = property.toNodeListProperty().toModelNodeList();
            for (const ModelNode &node : nodeList) {
                modelToTextMerger()->nodeRemoved(node, property.toNodeAbstractProperty(),
                                                 AbstractView::NoAdditionalChanges);
            }
        } else if (property.isBindingProperty() || property.isVariantProperty()
                   || property.isNodeProperty()) {
            // Default property that has actual binding/value should be removed.
            // We need to do it here in propertiesAboutToBeRemoved, because
            // type is no longer determinable after property is removed from the model.
            modelToTextMerger()->propertiesRemoved({property});
        }
    }
}

void RewriterView::propertiesRemoved(const QList<AbstractProperty>& propertyList)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->propertiesRemoved(propertyList);

    if (m_removeDefaultPropertyTransaction.isValid())
        m_removeDefaultPropertyTransaction.commit();

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::variantPropertiesChanged(const QList<VariantProperty>& propertyList, PropertyChangeFlags propertyChange)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    QList<AbstractProperty> usefulPropertyList;
    foreach (const VariantProperty &property, propertyList)
        usefulPropertyList.append(property);

    modelToTextMerger()->propertiesChanged(usefulPropertyList, propertyChange);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::bindingPropertiesChanged(const QList<BindingProperty>& propertyList, PropertyChangeFlags propertyChange)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    QList<AbstractProperty> usefulPropertyList;
    foreach (const BindingProperty &property, propertyList)
        usefulPropertyList.append(property);

    modelToTextMerger()->propertiesChanged(usefulPropertyList, propertyChange);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::signalHandlerPropertiesChanged(const QVector<SignalHandlerProperty> &propertyList, AbstractView::PropertyChangeFlags propertyChange)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    QList<AbstractProperty> usefulPropertyList;
    foreach (const SignalHandlerProperty &property, propertyList)
        usefulPropertyList.append(property);

    modelToTextMerger()->propertiesChanged(usefulPropertyList, propertyChange);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeReparented(const ModelNode &node, const NodeAbstractProperty &newPropertyParent, const NodeAbstractProperty &oldPropertyParent, AbstractView::PropertyChangeFlags propertyChange)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeReparented(node, newPropertyParent, oldPropertyParent, propertyChange);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::importsChanged(const QList<Import> &addedImports, const QList<Import> &removedImports)
{
    foreach (const Import &import, addedImports)
        importAdded(import);

    foreach (const Import &import, removedImports)
        importRemoved(import);

}

void RewriterView::importAdded(const Import &import)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    if (import.url() == QLatin1String("Qt")) {
        foreach (const Import &import, model()->imports()) {
            if (import.url() == QLatin1String("QtQuick"))
                return; //QtQuick magic we do not have to add an import for Qt
        }
    }

    modelToTextMerger()->addImport(import);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::importRemoved(const Import &import)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->removeImport(import);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeIdChanged(const ModelNode& node, const QString& newId, const QString& oldId)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeIdChanged(node, newId, oldId);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeOrderChanged(const NodeListProperty &listProperty,
                                    const ModelNode &movedNode,
                                    int /*oldIndex*/)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    ModelNode trailingNode;
    int newIndex = listProperty.indexOf(movedNode);
    if (newIndex + 1 < listProperty.count())
        trailingNode = listProperty.at(newIndex + 1);
    modelToTextMerger()->nodeSlidAround(movedNode, trailingNode);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeOrderChanged(const NodeListProperty &listProperty)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    auto modelNodes = listProperty.directSubNodes();

    for (const ModelNode &movedNode : modelNodes)
        modelToTextMerger()->nodeSlidAround(movedNode, ModelNode{});

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::rootNodeTypeChanged(const QString &type, int majorVersion, int minorVersion)
{
     Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeTypeChanged(rootModelNode(), type, majorVersion, minorVersion);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::nodeTypeChanged(const ModelNode &node, const TypeName &type, int majorVersion, int minorVersion)
{
    Q_ASSERT(textModifier());
    if (textToModelMerger()->isActive())
        return;

    modelToTextMerger()->nodeTypeChanged(node, QString::fromLatin1(type), majorVersion, minorVersion);

    if (!isModificationGroupActive())
        applyChanges();
}

void RewriterView::customNotification(const AbstractView * /*view*/, const QString &identifier, const QList<ModelNode> & /* nodeList */, const QList<QVariant> & /*data */)
{
    if (identifier == StartRewriterAmend || identifier == EndRewriterAmend)
        return; // we emitted this ourselves, so just ignore these notifications.
}

void RewriterView::rewriterBeginTransaction()
{
    transactionLevel++;
    setModificationGroupActive(true);
}

void RewriterView::rewriterEndTransaction()
{
    transactionLevel--;
    Q_ASSERT(transactionLevel >= 0);
    if (transactionLevel == 0)
    {
        setModificationGroupActive(false);
        applyModificationGroupChanges();
    }
}

bool RewriterView::isModificationGroupActive() const
{
    return m_modificationGroupActive;
}

void RewriterView::setModificationGroupActive(bool active)
{
    m_modificationGroupActive = active;
}

TextModifier *RewriterView::textModifier() const
{
    return m_textModifier;
}

void RewriterView::setTextModifier(TextModifier *textModifier)
{
    if (m_textModifier)
        disconnect(m_textModifier, &TextModifier::textChanged, this, &RewriterView::qmlTextChanged);

    m_textModifier = textModifier;

    if (m_textModifier)
        connect(m_textModifier, &TextModifier::textChanged, this, &RewriterView::qmlTextChanged);
}

QString RewriterView::textModifierContent() const
{
    if (textModifier())
        return textModifier()->text();

    return QString();
}

void RewriterView::reactivateTextMofifierChangeSignals()
{
    if (textModifier())
        textModifier()->reactivateChangeSignals();
}

void RewriterView::deactivateTextMofifierChangeSignals()
{
    if (textModifier())
        textModifier()->deactivateChangeSignals();
}

void RewriterView::auxiliaryDataChanged(const ModelNode &node, const PropertyName &name, const QVariant &)
{
    if (m_restoringAuxData)
        return;

    if (name.endsWith("@NodeInstance"))
        return;

    if (name.endsWith("@Internal"))
        return;

    if (node.isRootNode()) {
        if (name == "width" || name == "height" || name == "autoSize" || name == "formeditorColor"
            || name == "formeditorZoom")
            return;
    }

    m_textModifier->textDocument()->setModified(true);
}

void RewriterView::applyModificationGroupChanges()
{
    Q_ASSERT(transactionLevel == 0);
    applyChanges();
}

void RewriterView::applyChanges()
{
    if (modelToTextMerger()->hasNoPendingChanges())
        return; // quick exit: nothing to be done.

    clearErrorAndWarnings();

    if (inErrorState()) {
        const QString content = textModifierContent();
        qDebug().noquote() << "RewriterView::applyChanges() got called while in error state. Will do a quick-exit now.";
        qDebug().noquote() << "Content: " << content;
        throw RewritingException(__LINE__, __FUNCTION__, __FILE__, "RewriterView::applyChanges() already in error state", content);
    }

    m_differenceHandling = Validate;

    try {
        modelToTextMerger()->applyChanges();
        if (!errors().isEmpty())
            enterErrorState(errors().constFirst().description());
    } catch (const Exception &e) {
        const QString content = textModifierContent();
        qDebug().noquote() << "RewriterException:" << m_rewritingErrorMessage;
        qDebug().noquote() << "Content: " << qPrintable(content);
        enterErrorState(e.description());
    }

    m_differenceHandling = Amend;

    if (inErrorState()) {
        const QString content = textModifierContent();
        qDebug().noquote() << "RewriterException: " << m_rewritingErrorMessage;
        qDebug().noquote() << "Content: " << content;
        if (!errors().isEmpty())
            qDebug().noquote() << "Error:" << errors().constFirst().description();
        throw RewritingException(__LINE__, __FUNCTION__, __FILE__, qPrintable(m_rewritingErrorMessage), content);
    }
}

void RewriterView::amendQmlText()
{
    emitCustomNotification(StartRewriterAmend);

    const QString newQmlText = m_textModifier->text();

    ModelAmender differenceHandler(m_textToModelMerger.data());
    if (m_textToModelMerger->load(newQmlText, differenceHandler))
        m_lastCorrectQmlSource = newQmlText;
    emitCustomNotification(EndRewriterAmend);
}

void RewriterView::notifyErrorsAndWarnings(const QList<DocumentMessage> &errors)
{
    if (m_setWidgetStatusCallback)
        m_setWidgetStatusCallback(errors.isEmpty());

    emitDocumentMessage(errors, m_warnings);
}

static QString replaceIllegalPropertyNameChars(const QString &str)
{
    QString ret = str;

    ret.replace("@", "__AT__");

    return ret;
}

static bool idIsQmlKeyWord(const QString& id)
{
    static const QSet<QString> keywords = {
        "as",
        "break",
        "case",
        "catch",
        "continue",
        "debugger",
        "default",
        "delete",
        "do",
        "else",
        "finally",
        "for",
        "function",
        "if",
        "import",
        "in",
        "instanceof",
        "new",
        "return",
        "switch",
        "this",
        "throw",
        "try",
        "typeof",
        "var",
        "void",
        "while",
        "with"
    };

    return keywords.contains(id);
}

QString RewriterView::auxiliaryDataAsQML() const
{
    bool hasAuxData = false;

    setupCanonicalHashes();

    QString str = "Designer {\n    ";

    QTC_ASSERT(!m_canonicalIntModelNode.isEmpty(), return {});

    int columnCount = 0;

    const QRegularExpression safeName("^[a-z][a-zA-Z0-9]*$");

    for (const auto &node : allModelNodes()) {
        QHash<PropertyName, QVariant> data = node.auxiliaryData();
        if (!data.isEmpty()) {
            if (columnCount > 80) {
                str += "\n";
                columnCount = 0;
            }
            const int startLen = str.length();
            str += "D{";
            str += "i:";

            str += QString::number(m_canonicalModelNodeInt.value(node));
            str += ";";

            QStringList keys = Utils::transform(data.keys(), [](const PropertyName &name) {
                return QString::fromUtf8(name);
            });

            keys.sort();

            for (const QString &key : keys) {
                if (key.endsWith("@NodeInstance"))
                    continue;

                if (key.endsWith("@Internal"))
                    continue;

                if (idIsQmlKeyWord(key))
                    continue;

                if (!key.contains(safeName))
                    continue;
                hasAuxData = true;
                const QVariant value = data.value(key.toUtf8());
                QString strValue = value.toString();

                auto metaType = static_cast<QMetaType::Type>(value.type());

                if (metaType == QMetaType::QString
                        || metaType == QMetaType::QColor) {

                    strValue.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
                    strValue.replace(QStringLiteral("\""), QStringLiteral("\\\""));
                    strValue.replace(QStringLiteral("\t"), QStringLiteral("\\t"));
                    strValue.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
                    strValue.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
                    strValue.replace(QStringLiteral("*/"), QStringLiteral("*\\/"));

                    strValue = "\"" + strValue + "\"";
                }

                if (!strValue.isEmpty()) {
                    str += replaceIllegalPropertyNameChars(key) + ":";
                    str += strValue;
                    str += ";";
                }
            }

            if (str.endsWith(';'))
                str.chop(1);

            str += "}";
            columnCount += str.length() - startLen;
        }
    }

    str += "\n}\n";

    if (hasAuxData)
        return str;

    return {};
}

ModelNode RewriterView::getNodeForCanonicalIndex(int index)
{
    return m_canonicalIntModelNode.value(index);
}

void RewriterView::sanitizeModel()
{
    if (inErrorState())
        return;

    QmlObjectNode root = rootModelNode();

    QTC_ASSERT(root.isValid(), return);

    QList<ModelNode> danglingNodes;

    const auto danglingStates = root.allInvalidStateOperations();
    const auto danglingKeyframeGroups =  QmlTimelineKeyframeGroup::allInvalidTimelineKeyframeGroups(this);

    std::transform(danglingStates.begin(),
                   danglingStates.end(),
                   std::back_inserter(danglingNodes),
                   [](const auto &node) { return node.modelNode(); });

    std::transform(danglingKeyframeGroups.begin(),
                   danglingKeyframeGroups.end(),
                   std::back_inserter(danglingNodes),
                   [](const auto &node) { return node.modelNode(); });

    executeInTransaction("RewriterView::sanitizeModel", [&]() {
        for (auto node : std::as_const(danglingNodes))
            node.destroy();
    });
}

void RewriterView::setAllowComponentRoot(bool allow)
{
    m_allowComponentRoot = allow;
}

bool RewriterView::allowComponentRoot() const
{
    return m_allowComponentRoot;
}

Internal::ModelNodePositionStorage *RewriterView::positionStorage() const
{
    return m_positionStorage.data();
}

QList<DocumentMessage> RewriterView::warnings() const
{
    return m_warnings;
}

QList<DocumentMessage> RewriterView::errors() const
{
    return m_errors;
}

void RewriterView::clearErrorAndWarnings()
{
    m_errors.clear();
    m_warnings.clear();
    notifyErrorsAndWarnings(m_errors);
}

void RewriterView::setWarnings(const QList<DocumentMessage> &warnings)
{
    m_warnings = warnings;
    notifyErrorsAndWarnings(m_errors);
}

void RewriterView::setIncompleteTypeInformation(bool b)
{
    m_hasIncompleteTypeInformation = b;
}

bool RewriterView::hasIncompleteTypeInformation() const
{
    return m_hasIncompleteTypeInformation;
}

void RewriterView::setErrors(const QList<DocumentMessage> &errors)
{
    m_errors = errors;
    notifyErrorsAndWarnings(m_errors);
}

void RewriterView::addError(const DocumentMessage &error)
{
    m_errors.append(error);
    notifyErrorsAndWarnings(m_errors);
}

void RewriterView::enterErrorState(const QString &errorMessage)
{
    m_rewritingErrorMessage = errorMessage;
}

void RewriterView::resetToLastCorrectQml()
{
    m_textModifier->textDocument()->undo();
    m_textModifier->textDocument()->clearUndoRedoStacks(QTextDocument::RedoStack);
    ModelAmender differenceHandler(m_textToModelMerger.data());
    m_textToModelMerger->load(m_textModifier->text(), differenceHandler);

    leaveErrorState();
}

QMap<ModelNode, QString> RewriterView::extractText(const QList<ModelNode> &nodes) const
{
    QmlDesigner::ASTObjectTextExtractor extract(m_textModifier->text());
    QMap<ModelNode, QString> result;

    foreach (const ModelNode &node, nodes) {
        const int nodeLocation = m_positionStorage->nodeOffset(node);

        if (nodeLocation == ModelNodePositionStorage::INVALID_LOCATION)
            result.insert(node, QString());
        else
            result.insert(node, extract(nodeLocation));
    }

    return result;
}

int RewriterView::nodeOffset(const ModelNode &node) const
{
    return m_positionStorage->nodeOffset(node);
}

/**
 * \return the length of the node's text, or -1 if it wasn't found or if an error
 *         occurred.
 */
int RewriterView::nodeLength(const ModelNode &node) const
{
    ObjectLengthCalculator objectLengthCalculator;
    unsigned length;
    if (objectLengthCalculator(m_textModifier->text(), nodeOffset(node), length))
        return (int) length;
    else
        return -1;
}

int RewriterView::firstDefinitionInsideOffset(const ModelNode &node) const
{
    FirstDefinitionFinder firstDefinitionFinder(m_textModifier->text());
    return firstDefinitionFinder(nodeOffset(node));
}

int RewriterView::firstDefinitionInsideLength(const ModelNode &node) const
{
    FirstDefinitionFinder firstDefinitionFinder(m_textModifier->text());
    const int offset =  firstDefinitionFinder(nodeOffset(node));

    ObjectLengthCalculator objectLengthCalculator;
    unsigned length;
    if (objectLengthCalculator(m_textModifier->text(), offset, length))
        return length;
    else
        return -1;
}

bool RewriterView::modificationGroupActive()
{
    return m_modificationGroupActive;
}

static bool isInNodeDefinition(int nodeTextOffset, int nodeTextLength, int cursorPosition)
{
    return (nodeTextOffset <= cursorPosition) && (nodeTextOffset + nodeTextLength > cursorPosition);
}

ModelNode RewriterView::nodeAtTextCursorPositionHelper(const ModelNode &root, int cursorPosition) const
{
    using myPair = std::pair<ModelNode,int>;
    std::vector<myPair> data;

    for (const ModelNode &node : allModelNodes()) {
        int offset = nodeOffset(node);
        if (offset > 0)
            data.emplace_back(std::make_pair(node, offset));
    }

    std::sort(data.begin(), data.end(), [](myPair a, myPair b) {
        return a.second < b.second;
    });

    ModelNode lastNode = root;

    int i = 0;
    for (const myPair &pair : data) {
        ModelNode node = pair.first;
        i++;
        if (i >= int(data.size())) {
            lastNode = node;
            break;
        }

        const int nodeTextOffset = nodeOffset(node);
        const int nodeTextLength = m_textModifier->text().indexOf("}", nodeTextOffset) - nodeTextOffset - 1;

        if (isInNodeDefinition(nodeTextOffset, nodeTextLength, cursorPosition))
            lastNode = node;
        else if (nodeTextOffset > cursorPosition)
            break;
    }

    return lastNode;
}

void RewriterView::setupCanonicalHashes() const
{
    m_canonicalIntModelNode.clear();
    m_canonicalModelNodeInt.clear();

    using myPair = std::pair<ModelNode,int>;
    std::vector<myPair> data;

    for (const ModelNode &node : allModelNodes()) {
        int offset = nodeOffset(node);
        if (offset > 0)
            data.emplace_back(std::make_pair(node, offset));
    }

    std::sort(data.begin(), data.end(), [](myPair a, myPair b) {
        return a.second < b.second;
    });

    int i = 0;
    for (const myPair &pair : data) {
        m_canonicalIntModelNode.insert(i, pair.first);
        m_canonicalModelNodeInt.insert(pair.first, i);
        ++i;
    }
}

void RewriterView::handleLibraryInfoUpdate()
{
    // Trigger dummy amend to reload document when library info changes
    if (isAttached() && !m_modelAttachPending && !debugQmlPuppet())
        m_amendTimer.start();
}

void RewriterView::handleProjectUpdate()
{
    emit modelInterfaceProjectUpdated();
}

ModelNode RewriterView::nodeAtTextCursorPosition(int cursorPosition) const
{
    return nodeAtTextCursorPositionHelper(rootModelNode(), cursorPosition);
}

bool RewriterView::renameId(const QString& oldId, const QString& newId)
{
    if (textModifier()) {
        PropertyName propertyName = oldId.toUtf8();

        bool hasAliasExport = rootModelNode().isValid()
                && rootModelNode().hasBindingProperty(propertyName)
                && rootModelNode().bindingProperty(propertyName).isAliasExport();

        bool instant = m_instantQmlTextUpdate;
        m_instantQmlTextUpdate = true;

        bool refactoring =  textModifier()->renameId(oldId, newId);

        m_instantQmlTextUpdate = instant;

        if (refactoring && hasAliasExport) { //Keep export alias properties
            rootModelNode().removeProperty(propertyName);
            PropertyName newPropertyName = newId.toUtf8();
            rootModelNode().bindingProperty(newPropertyName).setDynamicTypeNameAndExpression("alias", QString::fromUtf8(newPropertyName));
        }
        return refactoring;
    }

    return false;
}

const QmlJS::ScopeChain *RewriterView::scopeChain() const
{
    return textToModelMerger()->scopeChain();
}

const QmlJS::Document *RewriterView::document() const
{
    return textToModelMerger()->document();
}

static inline QString getUrlFromType(const QString& typeName)
{
    QStringList nameComponents = typeName.split('.');
    QString result;

    for (int i = 0; i < (nameComponents.count() - 1); i++) {
        result += nameComponents.at(i);
    }

    return result;
}

QString RewriterView::convertTypeToImportAlias(const QString &type) const
{
    QString url;
    QString simplifiedType = type;
    if (type.contains('.')) {
        QStringList nameComponents = type.split('.');
        url = getUrlFromType(type);
        simplifiedType = nameComponents.constLast();
    }

    QString alias;
    if (!url.isEmpty()) {
        foreach (const Import &import, model()->imports()) {
            if (import.url() == url) {
                alias = import.alias();
                break;
            }
            if (import.file() == url) {
                alias = import.alias();
                break;
            }
        }
    }

    QString result;

    if (!alias.isEmpty())
        result = alias + '.';

    result += simplifiedType;

    return result;
}

QString RewriterView::pathForImport(const Import &import)
{
    if (scopeChain() && scopeChain()->context() && document()) {
        const QString importStr = import.isFileImport() ? import.file() : import.url();
        const QmlJS::Imports *imports = scopeChain()->context()->imports(document());

        QmlJS::ImportInfo importInfo;

        foreach (const QmlJS::Import &qmljsImport, imports->all()) {
            if (qmljsImport.info.name() == importStr)
                importInfo = qmljsImport.info;
        }
        const QString importPath = importInfo.path();
        return importPath;
    }

    return QString();
}

QStringList RewriterView::importDirectories() const
{
    return m_textToModelMerger->vContext().paths;
}

QSet<QPair<QString, QString> > RewriterView::qrcMapping() const
{
    return m_textToModelMerger->qrcMapping();
}

void RewriterView::moveToComponent(const ModelNode &modelNode)
{
    int offset = nodeOffset(modelNode);


    textModifier()->moveToComponent(offset);

}

QStringList RewriterView::autoComplete(const QString &text, int pos, bool explicitComplete)
{
    QTextDocument textDocument;
    textDocument.setPlainText(text);

    QStringList list = textModifier()->autoComplete(&textDocument, pos, explicitComplete);
    list.removeDuplicates();

    return list;
}

QList<QmlTypeData> RewriterView::getQMLTypes() const
{
    QList<QmlTypeData> qmlDataList;

    qmlDataList.append(m_textToModelMerger->getQMLSingletons());

    for (const QmlJS::ModelManagerInterface::CppData &cppData :
         QmlJS::ModelManagerInterface::instance()->cppData())
        for (const LanguageUtils::FakeMetaObject::ConstPtr &fakeMetaObject : cppData.exportedTypes) {
            for (const LanguageUtils::FakeMetaObject::Export &exportItem :
                 fakeMetaObject->exports()) {
                QmlTypeData qmlData;
                qmlData.cppClassName = fakeMetaObject->className();
                qmlData.typeName = exportItem.type;
                qmlData.importUrl = exportItem.package;
                qmlData.versionString = exportItem.version.toString();
                qmlData.superClassName = fakeMetaObject->superclassName();
                qmlData.isSingleton = fakeMetaObject->isSingleton();

                if (qmlData.importUrl != "<cpp>") //ignore pure unregistered cpp types
                    qmlDataList.append(qmlData);
            }
        }

    return qmlDataList;
}

void RewriterView::setWidgetStatusCallback(std::function<void (bool)> setWidgetStatusCallback)
{
    m_setWidgetStatusCallback = setWidgetStatusCallback;
}

void RewriterView::qmlTextChanged()
{
    if (inErrorState())
        return;

    if (m_textToModelMerger && m_textModifier) {
        const QString newQmlText = m_textModifier->text();

#if 0
        qDebug() << Q_FUNC_INFO;
        qDebug() << "old:" << lastCorrectQmlSource;
        qDebug() << "new:" << newQmlText;
#endif

        switch (m_differenceHandling) {
        case Validate: {
            ModelValidator differenceHandler(m_textToModelMerger.data());
            if (m_textToModelMerger->load(newQmlText, differenceHandler))
                m_lastCorrectQmlSource = newQmlText;
            break;
        }

        case Amend: {
            if (m_instantQmlTextUpdate) {
                amendQmlText();
            } else {
#ifndef QMLDESIGNER_TEST
                auto &viewManager = QmlDesignerPlugin::instance()->viewManager();
                if (viewManager.usesRewriterView(this)) {
                    QmlDesignerPlugin::instance()->viewManager().disableWidgets();
                    m_amendTimer.start();
                }
#else
                /*Keep test synchronous*/
                amendQmlText();
#endif
            }
            break;
        }
        }
    }
}

void RewriterView::delayedSetup()
{
    if (m_textToModelMerger)
        m_textToModelMerger->delayedSetup();
}

static QString annotationsEnd()
{
    const static QString end = QString("%1*/").arg(annotationsEscapeSequence);
    return end;
}

static QString annotationsStart()
{
    const static QString start = QString("/*%1").arg(annotationsEscapeSequence);
    return start;
}

QString RewriterView::getRawAuxiliaryData() const
{
    QTC_ASSERT(m_textModifier, return {});

    const QString oldText = m_textModifier->text();

    int startIndex = oldText.indexOf(annotationsStart());
    int endIndex = oldText.indexOf(annotationsEnd());

    if (startIndex > 0 && endIndex > 0)
        return oldText.mid(startIndex, endIndex - startIndex + annotationsEnd().length());

    return {};
}

void RewriterView::writeAuxiliaryData()
{
    QTC_ASSERT(m_textModifier, return);

    const QString oldText = m_textModifier->text();

    const int startIndex = oldText.indexOf(annotationsStart());
    const int endIndex = oldText.indexOf(annotationsEnd());

    QString auxData = auxiliaryDataAsQML();

    const bool replace = startIndex > 0 && endIndex > 0;

    if (!auxData.isEmpty()) {
        auxData.prepend("\n");
        auxData.prepend(annotationsStart());
        if (!replace)
            auxData.prepend("\n");
        auxData.append(annotationsEnd());
        if (!replace)
            auxData.append("\n");
    }

    if (replace)
        m_textModifier->replace(startIndex, endIndex - startIndex + annotationsEnd().length(), auxData);
    else
        m_textModifier->replace(oldText.length(), 0, auxData);
}

static void checkNode(const QmlJS::SimpleReaderNode::Ptr &node, RewriterView *view);

static void checkChildNodes(const QmlJS::SimpleReaderNode::Ptr &node, RewriterView *view)
{
    if (!node)
        return;

    for (const auto &child : node->children())
        checkNode(child, view);
}

static QString fixUpIllegalChars(const QString &str)
{
    QString ret = str;
    ret.replace("__AT__", "@");
    return ret;
}

void checkNode(const QmlJS::SimpleReaderNode::Ptr &node, RewriterView *view)
{
    if (!node)
        return;

    if (!node->propertyNames().contains("i"))
        return;

    const int index = node->property("i").value.toInt();

    const ModelNode modelNode = view->getNodeForCanonicalIndex(index);

    if (!modelNode.isValid())
        return;

    auto properties = node->properties();

    for (auto i = properties.begin(); i != properties.end(); ++i) {
        if (i.key() != "i") {
            const PropertyName name = fixUpIllegalChars(i.key()).toUtf8();
            if (!modelNode.hasAuxiliaryData(name))
                modelNode.setAuxiliaryData(name, i.value().value);
        }
    }

    checkChildNodes(node, view);
}

void RewriterView::restoreAuxiliaryData()
{
    QTC_ASSERT(m_textModifier, return);

    const char auxRestoredFlag[] = "AuxRestored@Internal";
    if (rootModelNode().hasAuxiliaryData(auxRestoredFlag))
        return;

    m_restoringAuxData = true;

    setupCanonicalHashes();

    if (m_canonicalIntModelNode.isEmpty())
        return;

    const QString text = m_textModifier->text();

    int startIndex = text.indexOf(annotationsStart());
    int endIndex = text.indexOf(annotationsEnd());

    if (startIndex > 0 && endIndex > 0) {
        const QString auxSource = text.mid(startIndex + annotationsStart().length(),
                                           endIndex - startIndex - annotationsStart().length());
        QmlJS::SimpleReader reader;
        checkChildNodes(reader.readFromSource(auxSource), this);
    }

    rootModelNode().setAuxiliaryData(auxRestoredFlag, true);
    m_restoringAuxData = false;
}

} //QmlDesigner
