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

#include "qmldesignerplugin.h"
#include "cmakeprojectconverter.h"
#include "designmodecontext.h"
#include "designmodewidget.h"
#include "exception.h"
#include "generatecmakelists.h"
#include "generateresource.h"
#include "nodeinstanceview.h"
#include "openuiqmlfiledialog.h"
#include "qmldesignerconstants.h"
#include "qmldesignerprojectmanager.h"
#include "settingspage.h"

#include <metainfo.h>
#include <connectionview.h>
#include <sourcetool/sourcetool.h>
#include <colortool/colortool.h>
#include <curveeditor/curveeditorview.h>
#include <formeditor/transitiontool.h>
#include <texttool/texttool.h>
#include <timelineeditor/timelineview.h>
#include <transitioneditor/transitioneditorview.h>
#include <eventlist/eventlistpluginview.h>
#include <pathtool/pathtool.h>

#include <qmljseditor/qmljseditor.h>
#include <qmljseditor/qmljseditorconstants.h>
#include <qmljseditor/qmljseditordocument.h>

#include <qmljstools/qmljstoolsconstants.h>

#include <qmlprojectmanager/qmlproject.h>

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/designmode.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/messagebox.h>
#include <coreplugin/modemanager.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>
#include <sqlitelibraryinitializer.h>
#include <qmljs/qmljsmodelmanagerinterface.h>

#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/algorithm.h>

#include <QAction>
#include <QTimer>
#include <QCoreApplication>
#include <qplugin.h>
#include <QDebug>
#include <QProcessEnvironment>
#include <QScreen>
#include <QWindow>
#include <QApplication>

#include "nanotrace/nanotrace.h"
#include <modelnodecontextmenu_helper.h>

static Q_LOGGING_CATEGORY(qmldesignerLog, "qtc.qmldesigner", QtWarningMsg)

using namespace QmlDesigner::Internal;

namespace QmlDesigner {

namespace Internal {

class QtQuickDesignerFactory : public QmlJSEditor::QmlJSEditorFactory
{
public:
    QtQuickDesignerFactory();
};

QtQuickDesignerFactory::QtQuickDesignerFactory()
    : QmlJSEditorFactory(QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID)
{
    setDisplayName(QCoreApplication::translate("OpenWith::Editors", "Qt Quick Designer"));

    addMimeType(QmlJSTools::Constants::QMLUI_MIMETYPE);
    setDocumentCreator([this]() {
        auto document = new QmlJSEditor::QmlJSEditorDocument(id());
        document->setIsDesignModePreferred(
                    QmlDesigner::DesignerSettings::getValue(
                        QmlDesigner::DesignerSettingsKey::ALWAYS_DESIGN_MODE).toBool());
        return document;
    });
}

} // namespace Internal

class QmlDesignerPluginPrivate
{
public:
    QmlDesignerProjectManager projectManager;
    ViewManager viewManager;
    DocumentManager documentManager;
    ShortCutManager shortCutManager;
    SettingsPage settingsPage;
    DesignModeWidget mainWidget;
    DesignerSettings settings;
    QtQuickDesignerFactory m_qtQuickDesignerFactory;
    bool blockEditorChange = false;
};

QmlDesignerPlugin *QmlDesignerPlugin::m_instance = nullptr;

static bool isInDesignerMode()
{
    return Core::ModeManager::currentModeId() == Core::Constants::MODE_DESIGN;
}

static bool checkIfEditorIsQtQuick(Core::IEditor *editor)
{
    if (editor
        && (editor->document()->id() == QmlJSEditor::Constants::C_QMLJSEDITOR_ID
            || editor->document()->id() == QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID)) {
        QmlJS::ModelManagerInterface *modelManager = QmlJS::ModelManagerInterface::instance();
        QmlJS::Document::Ptr document = modelManager->ensuredGetDocumentForPath(editor->document()->filePath().toString());
        if (!document.isNull())
            return document->language() == QmlJS::Dialect::QmlQtQuick2
                    || document->language() == QmlJS::Dialect::QmlQtQuick2Ui
                    || document->language() == QmlJS::Dialect::Qml;

        if (Core::ModeManager::currentModeId() == Core::Constants::MODE_DESIGN) {
            Core::AsynchronousMessageBox::warning(QmlDesignerPlugin::tr("Cannot Open Design Mode"),
                                                  QmlDesignerPlugin::tr("The QML file is not currently opened in a QML Editor."));
            Core::ModeManager::activateMode(Core::Constants::MODE_EDIT);
        }
    }

    return false;
}

static bool isDesignerMode(Utils::Id mode)
{
    return mode == Core::Constants::MODE_DESIGN;
}

static bool documentIsAlreadyOpen(DesignDocument *designDocument, Core::IEditor *editor, Utils::Id newMode)
{
    return designDocument
            && editor == designDocument->editor()
            && isDesignerMode(newMode);
}

static bool shouldAssertInException()
{
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    return !processEnvironment.value("QMLDESIGNER_ASSERT_ON_EXCEPTION").isEmpty();
}

static bool warningsForQmlFilesInsteadOfUiQmlEnabled()
{
    return DesignerSettings::getValue(DesignerSettingsKey::WARNING_FOR_QML_FILES_INSTEAD_OF_UIQML_FILES).toBool();
}

QmlDesignerPlugin::QmlDesignerPlugin()
{
    m_instance = this;
    // Exceptions should never ever assert: they are handled in a number of
    // places where it is actually VALID AND EXPECTED BEHAVIOUR to get an
    // exception.
    // If you still want to see exactly where the exception originally
    // occurred, then you have various ways to do this:
    //  1. set a breakpoint on the constructor of the exception
    //  2. in gdb: "catch throw" or "catch throw Exception"
    //  3. set a breakpoint on __raise_exception()
    // And with gdb, you can even do this from your ~/.gdbinit file.
    // DnD is not working with gdb so this is still needed to get a good stacktrace

    Exception::setShouldAssert(shouldAssertInException());
}

QmlDesignerPlugin::~QmlDesignerPlugin()
{
    if (d)
        Core::DesignMode::unregisterDesignWidget(&d->mainWidget);
    delete d;
    d = nullptr;
    m_instance = nullptr;
}

////////////////////////////////////////////////////
//
// INHERITED FROM ExtensionSystem::Plugin
//
////////////////////////////////////////////////////
bool QmlDesignerPlugin::initialize(const QStringList & /*arguments*/, QString *errorMessage/* = 0*/)
{
    Sqlite::LibraryInitializer::initialize();
    QDir{}.mkpath(Core::ICore::cacheResourcePath().toString());

    if (!Utils::HostOsInfo::canCreateOpenGLContext(errorMessage))
        return false;
    d = new QmlDesignerPluginPrivate;
    if (QmlProjectManager::QmlProject::isQtDesignStudio())
        GenerateResource::generateMenuEntry();

    GenerateCmake::generateMenuEntry();
    GenerateCmake::CmakeProjectConverter::generateMenuEntry();

    const QString fontPath
        = Core::ICore::resourcePath(
                "qmldesigner/propertyEditorQmlSources/imports/StudioTheme/icons.ttf")
              .toString();
    if (QFontDatabase::addApplicationFont(fontPath) < 0)
        qCWarning(qmldesignerLog) << "Could not add font " << fontPath << "to font database";

#ifdef NANOTRACE_ENABLED
    auto handleShutdownNanotraceAction = [](const SelectionContext &) {};
    auto shutdownNanotraceIcon = []() { return QIcon(); };
    auto startNanotraceAction = new ModelNodeAction("Start Nanotrace",
                     QObject::tr("Start Nanotrace"),
                     shutdownNanotraceIcon(),
                     QObject::tr("Start Nanotrace"),
                     ComponentCoreConstants::eventListCategory,
                     QKeySequence(),
                     220,
                     handleShutdownNanotraceAction);

    connect(startNanotraceAction->defaultAction(), &QAction::triggered, [this]() {
        d->viewManager.nodeInstanceView()->startNanotrace();
    });

    designerActionManager().addDesignerAction(startNanotraceAction);

    auto shutDownNanotraceAction = new ModelNodeAction("ShutDown Nanotrace",
                      QObject::tr("Shut Down Nanotrace"),
                      shutdownNanotraceIcon(),
                      QObject::tr("Shut Down Nanotrace"),
                      ComponentCoreConstants::eventListCategory,
                      QKeySequence(),
                      220,
                      handleShutdownNanotraceAction);

    connect(shutDownNanotraceAction->defaultAction(), &QAction::triggered, [this]() {
        d->viewManager.nodeInstanceView()->endNanotrace();
    });

    designerActionManager().addDesignerAction(shutDownNanotraceAction);
#endif

    return true;
}

bool QmlDesignerPlugin::delayedInitialize()
{
    // adding default path to item library plugins
    const QString postfix = Utils::HostOsInfo::isMacHost() ? QString("/QmlDesigner")
                                                           : QString("/qmldesigner");
    const QStringList pluginPaths =
        Utils::transform(ExtensionSystem::PluginManager::pluginPaths(), [postfix](const QString &p) {
            return QString(p + postfix);
        });
    MetaInfo::setPluginPaths(pluginPaths);

    d->settings.fromSettings(Core::ICore::settings());

    d->viewManager.registerView(std::make_unique<QmlDesigner::Internal::ConnectionView>());
    if (DesignerSettings::getValue(DesignerSettingsKey::ENABLE_TIMELINEVIEW).toBool()) {
        auto timelineView = d->viewManager.registerView(std::make_unique<QmlDesigner::TimelineView>());
        timelineView->registerActions();

        d->viewManager.registerView(std::make_unique<QmlDesigner::CurveEditorView>());

        auto eventlistView = d->viewManager.registerView(
            std::make_unique<QmlDesigner::EventListPluginView>());
        eventlistView->registerActions();
    }

    auto transitionEditorView = d->viewManager.registerView(
        std::make_unique<QmlDesigner::TransitionEditorView>());
    transitionEditorView->registerActions();

    d->viewManager.registerFormEditorTool(std::make_unique<QmlDesigner::SourceTool>());
    d->viewManager.registerFormEditorTool(std::make_unique<QmlDesigner::ColorTool>());
    d->viewManager.registerFormEditorTool(std::make_unique<QmlDesigner::TextTool>());
    d->viewManager.registerFormEditorTool(std::make_unique<QmlDesigner::PathTool>());
    d->viewManager.registerFormEditorTool(std::make_unique<QmlDesigner::TransitionTool>());

    if (QmlProjectManager::QmlProject::isQtDesignStudio()) {
        emitUsageStatistics("StandaloneMode");
        if (QmlProjectManager::QmlProject::isQtDesignStudioStartedFromQtC())
            emitUsageStatistics("QDSlaunchedFromQtC");
    }

    return true;
}

void QmlDesignerPlugin::extensionsInitialized()
{
    Core::DesignMode::setDesignModeIsRequired();
    // delay after Core plugin's extensionsInitialized, so the DesignMode is availabe
    connect(Core::ICore::instance(), &Core::ICore::coreAboutToOpen, this, [this] {
        integrateIntoQtCreator(&d->mainWidget);
    });
}

static QStringList allUiQmlFilesforCurrentProject(const Utils::FilePath &fileName)
{
    QStringList list;
    ProjectExplorer::Project *currentProject = ProjectExplorer::SessionManager::projectForFile(fileName);

    if (currentProject) {
        foreach (const Utils::FilePath &fileName, currentProject->files(ProjectExplorer::Project::SourceFiles)) {
            if (fileName.endsWith(".ui.qml"))
                list.append(fileName.toString());
        }
    }

    return list;
}

static QString projectPath(const Utils::FilePath &fileName)
{
    QString path;
    ProjectExplorer::Project *currentProject = ProjectExplorer::SessionManager::projectForFile(fileName);

    if (currentProject)
        path = currentProject->projectDirectory().toString();

    return path;
}

void QmlDesignerPlugin::integrateIntoQtCreator(QWidget *modeWidget)
{
    auto context = new Internal::DesignModeContext(modeWidget);
    Core::ICore::addContextObject(context);
    Core::Context qmlDesignerMainContext(Constants::C_QMLDESIGNER);
    Core::Context qmlDesignerFormEditorContext(Constants::C_QMLFORMEDITOR);
    Core::Context qmlDesignerEditor3dContext(Constants::C_QMLEDITOR3D);
    Core::Context qmlDesignerNavigatorContext(Constants::C_QMLNAVIGATOR);

    context->context().add(qmlDesignerMainContext);
    context->context().add(qmlDesignerFormEditorContext);
    context->context().add(qmlDesignerEditor3dContext);
    context->context().add(qmlDesignerNavigatorContext);
    context->context().add(ProjectExplorer::Constants::QMLJS_LANGUAGE_ID);

    d->shortCutManager.registerActions(qmlDesignerMainContext, qmlDesignerFormEditorContext,
                                       qmlDesignerEditor3dContext, qmlDesignerNavigatorContext);

    const QStringList mimeTypes = { QmlJSTools::Constants::QML_MIMETYPE,
                                    QmlJSTools::Constants::QMLUI_MIMETYPE };

    Core::DesignMode::registerDesignWidget(modeWidget, mimeTypes, context->context());

    connect(Core::DesignMode::instance(), &Core::DesignMode::actionsUpdated,
        &d->shortCutManager, &ShortCutManager::updateActions);

    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, [this] (Core::IEditor *editor) {
        if (d && checkIfEditorIsQtQuick(editor) && isInDesignerMode())
            changeEditor();
    });

    connect(Core::EditorManager::instance(), &Core::EditorManager::editorsClosed, [this] (QList<Core::IEditor*> editors) {
        if (d) {
            if (d->documentManager.hasCurrentDesignDocument()
                    && editors.contains(currentDesignDocument()->textEditor()))
                hideDesigner();

            d->documentManager.removeEditors(editors);
        }
    });

    connect(Core::ModeManager::instance(),
            &Core::ModeManager::currentModeChanged,
            [this](Utils::Id newMode, Utils::Id oldMode) {
                Core::IEditor *currentEditor = Core::EditorManager::currentEditor();
                if (d && currentEditor && checkIfEditorIsQtQuick(currentEditor)
                    && !documentIsAlreadyOpen(currentDesignDocument(), currentEditor, newMode)) {
                    if (isDesignerMode(newMode)) {
                        showDesigner();
                    } else if (currentDesignDocument()
                               || (!isDesignerMode(newMode) && isDesignerMode(oldMode))) {
                        hideDesigner();
                    }
                }
            });
}

void QmlDesignerPlugin::showDesigner()
{
    QTC_ASSERT(!d->documentManager.hasCurrentDesignDocument(), return);

    d->mainWidget.initialize();

    const Utils::FilePath fileName = Core::EditorManager::currentEditor()->document()->filePath();
    const QStringList allUiQmlFiles = allUiQmlFilesforCurrentProject(fileName);
    if (warningsForQmlFilesInsteadOfUiQmlEnabled() && !fileName.endsWith(".ui.qml") && !allUiQmlFiles.isEmpty()) {
        OpenUiQmlFileDialog dialog(&d->mainWidget);
        dialog.setUiQmlFiles(projectPath(fileName), allUiQmlFiles);
        dialog.exec();
        if (dialog.uiFileOpened()) {
            Core::ModeManager::activateMode(Core::Constants::MODE_EDIT);
            Core::EditorManager::openEditorAt(
                {Utils::FilePath::fromString(dialog.uiQmlFile()), 0, 0});
            return;
        }
    }

    d->shortCutManager.disconnectUndoActions(currentDesignDocument());
    d->documentManager.setCurrentDesignDocument(Core::EditorManager::currentEditor());
    d->shortCutManager.connectUndoActions(currentDesignDocument());

    if (d->documentManager.hasCurrentDesignDocument()) {
        activateAutoSynchronization();
        d->shortCutManager.updateActions(currentDesignDocument()->textEditor());
        d->viewManager.pushFileOnCrumbleBar(currentDesignDocument()->fileName());
    }

    d->shortCutManager.updateUndoActions(currentDesignDocument());
}

void QmlDesignerPlugin::hideDesigner()
{
    if (d->documentManager.hasCurrentDesignDocument()) {
        deactivateAutoSynchronization();
        d->mainWidget.saveSettings();
    }

    d->shortCutManager.disconnectUndoActions(currentDesignDocument());
    d->documentManager.setCurrentDesignDocument(nullptr);
    d->shortCutManager.updateUndoActions(nullptr);
}

void QmlDesignerPlugin::changeEditor()
{
    if (d->blockEditorChange)
         return;

    if (d->documentManager.hasCurrentDesignDocument()) {
        deactivateAutoSynchronization();
        d->mainWidget.saveSettings();
    }

    d->shortCutManager.disconnectUndoActions(currentDesignDocument());
    d->documentManager.setCurrentDesignDocument(Core::EditorManager::currentEditor());
    d->mainWidget.initialize();
    d->shortCutManager.connectUndoActions(currentDesignDocument());

    if (d->documentManager.hasCurrentDesignDocument()) {
        activateAutoSynchronization();
        d->viewManager.pushFileOnCrumbleBar(currentDesignDocument()->fileName());
        d->viewManager.setComponentViewToMaster();
    }

    d->shortCutManager.updateUndoActions(currentDesignDocument());
}

void QmlDesignerPlugin::jumpTextCursorToSelectedModelNode()
{
    // visual editor -> text editor
    ModelNode selectedNode;
    if (!rewriterView()->selectedModelNodes().isEmpty())
        selectedNode = rewriterView()->selectedModelNodes().constFirst();

    if (selectedNode.isValid()) {
        const int nodeOffset = rewriterView()->nodeOffset(selectedNode);
        if (nodeOffset > 0) {
            const ModelNode currentSelectedNode = rewriterView()->
                nodeAtTextCursorPosition(currentDesignDocument()->plainTextEdit()->textCursor().position());
            if (currentSelectedNode != selectedNode) {
                int line, column;
                currentDesignDocument()->textEditor()->convertPosition(nodeOffset, &line, &column);
                // line has to be 1 based, column 0 based!
                currentDesignDocument()->textEditor()->gotoLine(line, column - 1);
            }
        }
    }
}

void QmlDesignerPlugin::selectModelNodeUnderTextCursor()
{
    const int cursorPosition = currentDesignDocument()->plainTextEdit()->textCursor().position();
    ModelNode modelNode = rewriterView()->nodeAtTextCursorPosition(cursorPosition);
    if (modelNode.isValid())
        rewriterView()->setSelectedModelNode(modelNode);
}

void QmlDesignerPlugin::activateAutoSynchronization()
{
    // text editor -> visual editor
    if (!currentDesignDocument()->isDocumentLoaded())
        currentDesignDocument()->loadDocument(currentDesignDocument()->plainTextEdit());

    currentDesignDocument()->updateActiveTarget();
    d->mainWidget.enableWidgets();
    currentDesignDocument()->attachRewriterToModel();

    resetModelSelection();

    viewManager().attachComponentView();
    viewManager().attachViewsExceptRewriterAndComponetView();

    selectModelNodeUnderTextCursor();

    d->mainWidget.setupNavigatorHistory(currentDesignDocument()->textEditor());

    currentDesignDocument()->updateSubcomponentManager();
}

void QmlDesignerPlugin::deactivateAutoSynchronization()
{
    viewManager().detachViewsExceptRewriterAndComponetView();
    viewManager().detachComponentView();
    viewManager().detachRewriterView();
    documentManager().currentDesignDocument()->resetToDocumentModel();
}

void QmlDesignerPlugin::resetModelSelection()
{
    if (!rewriterView()) {
        qCWarning(qmldesignerLog) << "No rewriter existing while calling resetModelSelection";
        return;
    }
    if (!currentModel()) {
        qCWarning(qmldesignerLog) << "No current QmlDesigner document model while calling resetModelSelection";
        return;
    }
    rewriterView()->setSelectedModelNodes(QList<ModelNode>());
}

RewriterView *QmlDesignerPlugin::rewriterView() const
{
    return currentDesignDocument()->rewriterView();
}

Model *QmlDesignerPlugin::currentModel() const
{
    return currentDesignDocument()->currentModel();
}

DesignDocument *QmlDesignerPlugin::currentDesignDocument() const
{
    if (d)
        return d->documentManager.currentDesignDocument();

    return nullptr;
}

Internal::DesignModeWidget *QmlDesignerPlugin::mainWidget() const
{
    if (d)
        return &d->mainWidget;

    return nullptr;
}

QWidget *QmlDesignerPlugin::createProjectExplorerWidget(QWidget *parent) const
{
    return Internal::DesignModeWidget::createProjectExplorerWidget(parent);
}

void QmlDesignerPlugin::switchToTextModeDeferred()
{
    QTimer::singleShot(0, this, [] () {
        Core::ModeManager::activateMode(Core::Constants::MODE_EDIT);
    });
}

void QmlDesignerPlugin::emitCurrentTextEditorChanged(Core::IEditor *editor)
{
    d->blockEditorChange = true;
    emit Core::EditorManager::instance()->currentEditorChanged(editor);
    d->blockEditorChange = false;
}

double QmlDesignerPlugin::formEditorDevicePixelRatio()
{
    if (DesignerSettings::getValue(DesignerSettingsKey::IGNORE_DEVICE_PIXEL_RATIO).toBool())
        return 1;

    const QList<QWindow *> topLevelWindows = QApplication::topLevelWindows();
    if (topLevelWindows.isEmpty())
        return 1;
    return topLevelWindows.constFirst()->screen()->devicePixelRatio();
}

void QmlDesignerPlugin::emitUsageStatistics(const QString &identifier)
{
    QTC_ASSERT(instance(), return);
    emit instance()->usageStatisticsNotifier(identifier);
}

void QmlDesignerPlugin::emitUsageStatisticsContextAction(const QString &identifier)
{
    emitUsageStatistics(Constants::EVENT_ACTION_EXECUTED + identifier);
}

void QmlDesignerPlugin::emitUsageStatisticsHelpRequested(const QString &identifier)
{
    emitUsageStatistics(Constants::EVENT_HELP_REQUESTED + identifier);
}

AsynchronousImageCache &QmlDesignerPlugin::imageCache()
{
    return m_instance->d->viewManager.imageCache();
}

void QmlDesignerPlugin::registerPreviewImageProvider(QQmlEngine *engine)
{
    m_instance->d->projectManager.registerPreviewImageProvider(engine);
}

void QmlDesignerPlugin::emitUsageStatisticsTime(const QString &identifier, int elapsed)
{
    emit instance()->usageStatisticsUsageTimer(identifier, elapsed);
}

QmlDesignerPlugin *QmlDesignerPlugin::instance()
{
    return m_instance;
}

DocumentManager &QmlDesignerPlugin::documentManager()
{
    return d->documentManager;
}

const DocumentManager &QmlDesignerPlugin::documentManager() const
{
    return d->documentManager;
}

ViewManager &QmlDesignerPlugin::viewManager()
{
    return d->viewManager;
}

const ViewManager &QmlDesignerPlugin::viewManager() const
{
    return d->viewManager;
}

DesignerActionManager &QmlDesignerPlugin::designerActionManager()
{
    return d->viewManager.designerActionManager();
}

const DesignerActionManager &QmlDesignerPlugin::designerActionManager() const
{
    return d->viewManager.designerActionManager();
}

DesignerSettings QmlDesignerPlugin::settings()
{
    d->settings.fromSettings(Core::ICore::settings());
    return d->settings;
}

void QmlDesignerPlugin::setSettings(const DesignerSettings &s)
{
    if (s != d->settings) {
        d->settings = s;
        d->settings.toSettings(Core::ICore::settings());
    }
}

} // namespace QmlDesigner
