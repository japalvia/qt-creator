add_qtc_library(Utils
  DEPENDS Qt5::Qml Qt5::Xml
  PUBLIC_DEPENDS
    Qt5::Concurrent Qt5::Core Qt5::Network Qt5::Gui Qt5::Widgets
    Qt6Core5Compat
  SOURCES
    ../3rdparty/optional/optional.hpp
    ../3rdparty/variant/variant.hpp
    ../3rdparty/span/span.hpp
    QtConcurrentTools
    algorithm.h
    ansiescapecodehandler.cpp ansiescapecodehandler.h
    appmainwindow.cpp appmainwindow.h
    archive.cpp archive.h
    aspects.cpp aspects.h
    basetreeview.cpp basetreeview.h
    benchmarker.cpp benchmarker.h
    buildablehelperlibrary.cpp buildablehelperlibrary.h
    camelcasecursor.cpp camelcasecursor.h
    categorysortfiltermodel.cpp categorysortfiltermodel.h
    changeset.cpp changeset.h
    checkablemessagebox.cpp checkablemessagebox.h
    classnamevalidatinglineedit.cpp classnamevalidatinglineedit.h
    codegeneration.cpp codegeneration.h
    commandline.cpp commandline.h
    completinglineedit.cpp completinglineedit.h
    completingtextedit.cpp completingtextedit.h
    cpplanguage_details.h
    crumblepath.cpp crumblepath.h
    declarationmacros.h
    delegates.cpp delegates.h
    detailsbutton.cpp detailsbutton.h
    detailswidget.cpp detailswidget.h
    differ.cpp differ.h
    displayname.cpp displayname.h
    dropsupport.cpp dropsupport.h
    elfreader.cpp elfreader.h
    elidinglabel.cpp elidinglabel.h
    environment.cpp environment.h
    environmentdialog.cpp environmentdialog.h
    environmentfwd.h
    environmentmodel.cpp environmentmodel.h
    execmenu.cpp execmenu.h
    executeondestruction.h
    fadingindicator.cpp fadingindicator.h
    faketooltip.cpp faketooltip.h
    fancylineedit.cpp fancylineedit.h
    fancymainwindow.cpp fancymainwindow.h
    filecrumblabel.cpp filecrumblabel.h
    fileinprojectfinder.cpp fileinprojectfinder.h
    filenamevalidatinglineedit.cpp filenamevalidatinglineedit.h
    filepath.cpp filepath.h
    filesearch.cpp filesearch.h
    filesystemmodel.cpp filesystemmodel.h
    filesystemwatcher.cpp filesystemwatcher.h
    fileutils.cpp fileutils.h
    filewizardpage.cpp filewizardpage.h
    fixedsizeclicklabel.cpp fixedsizeclicklabel.h
    flowlayout.cpp flowlayout.h
    functiontraits.h
    futuresynchronizer.cpp futuresynchronizer.h
    fuzzymatcher.cpp fuzzymatcher.h
    genericconstants.h
    globalfilechangeblocker.cpp globalfilechangeblocker.h
    guard.cpp guard.h
    headerviewstretcher.cpp headerviewstretcher.h
    highlightingitemdelegate.cpp highlightingitemdelegate.h
    historycompleter.cpp historycompleter.h
    hostosinfo.cpp hostosinfo.h
    htmldocextractor.cpp htmldocextractor.h
    icon.cpp icon.h
    id.cpp id.h
    indexedcontainerproxyconstiterator.h
    infobar.cpp infobar.h
    infolabel.cpp infolabel.h
    itemviews.cpp itemviews.h
    json.cpp json.h
    jsontreeitem.cpp jsontreeitem.h
    launcherinterface.cpp launcherinterface.h
    launcherpackets.cpp launcherpackets.h
    launchersocket.cpp launchersocket.h
    layoutbuilder.cpp layoutbuilder.h
    linecolumn.cpp linecolumn.h
    link.cpp link.h
    listmodel.h
    listutils.h
    macroexpander.cpp macroexpander.h
    mapreduce.h
    mimeutils.h
    multitextcursor.cpp multitextcursor.h
    namevaluedictionary.cpp namevaluedictionary.h
    namevaluedictionary.cpp namevaluedictionary.h
    namevalueitem.cpp namevalueitem.h
    namevalueitem.cpp namevalueitem.h
    namevaluemodel.cpp namevaluemodel.h
    namevaluemodel.cpp namevaluemodel.h
    namevaluesdialog.cpp namevaluesdialog.h
    namevaluesdialog.cpp namevaluesdialog.h
    namevaluevalidator.cpp namevaluevalidator.h
    namevaluevalidator.cpp namevaluevalidator.h
    navigationtreeview.cpp navigationtreeview.h
    networkaccessmanager.cpp networkaccessmanager.h
    optional.h
    osspecificaspects.h
    outputformat.h
    outputformatter.cpp outputformatter.h
    overlaywidget.cpp overlaywidget.h
    overridecursor.cpp overridecursor.h
    parameteraction.cpp parameteraction.h
    pathchooser.cpp pathchooser.h
    pathlisteditor.cpp pathlisteditor.h
    persistentsettings.cpp persistentsettings.h
    pointeralgorithm.h
    port.cpp port.h
    porting.h
    portlist.cpp portlist.h
    predicates.h
    processenums.h
    processhandle.cpp processhandle.h
    processinfo.cpp processinfo.h
    processinterface.h
    processreaper.cpp processreaper.h
    processutils.cpp processutils.h
    progressindicator.cpp progressindicator.h
    projectintropage.cpp projectintropage.h projectintropage.ui
    proxyaction.cpp proxyaction.h
    proxycredentialsdialog.cpp proxycredentialsdialog.h proxycredentialsdialog.ui
    qrcparser.cpp qrcparser.h
    qtcassert.cpp qtcassert.h
    qtcolorbutton.cpp qtcolorbutton.h
    qtcprocess.cpp qtcprocess.h
    qtcsettings.cpp qtcsettings.h
    reloadpromptutils.cpp reloadpromptutils.h
    removefiledialog.cpp removefiledialog.h removefiledialog.ui
    runextensions.cpp runextensions.h
    savefile.cpp savefile.h
    scopedswap.h
    set_algorithm.h
    settingsaccessor.cpp settingsaccessor.h
    settingsselector.cpp settingsselector.h
    settingsutils.h
    shellcommand.cpp shellcommand.h
    shellcommandpage.cpp shellcommandpage.h
    singleton.cpp singleton.h
    sizedarray.h
    smallstring.h
    smallstringfwd.h
    smallstringio.h
    smallstringiterator.h
    smallstringlayout.h
    smallstringliteral.h
    smallstringmemory.h
    smallstringvector.h
    smallstringview.h
    span.h
    statuslabel.cpp statuslabel.h
    stringutils.cpp stringutils.h
    styledbar.cpp styledbar.h
    stylehelper.cpp stylehelper.h
    templateengine.cpp templateengine.h
    temporarydirectory.cpp temporarydirectory.h
    temporaryfile.cpp temporaryfile.h
    terminalcommand.cpp terminalcommand.h
    terminalprocess.cpp terminalprocess_p.h
    textfieldcheckbox.cpp textfieldcheckbox.h
    textfieldcombobox.cpp textfieldcombobox.h
    textfileformat.cpp textfileformat.h
    textutils.cpp textutils.h
    theme/theme.cpp theme/theme.h theme/theme_p.h
    threadutils.cpp threadutils.h
    tooltip/effects.h
    tooltip/tips.cpp tooltip/tips.h
    tooltip/tooltip.cpp tooltip/tooltip.h
    touchbar/touchbar.h
    treemodel.cpp treemodel.h
    treeviewcombobox.cpp treeviewcombobox.h
    uncommentselection.cpp uncommentselection.h
    unixutils.cpp unixutils.h
    url.cpp url.h
    utils.qrc
    utils_global.h
    utilsicons.cpp utilsicons.h
    variablechooser.cpp variablechooser.h
    variant.h
    winutils.cpp winutils.h
    wizard.cpp wizard.h
    wizardpage.cpp wizardpage.h
)

option(QTC_USE_NEW_MIMEDATABASE "Use updated MIME database implementation" NO)

if(QTC_USE_NEW_MIMEDATABASE)
  set(mime_prefix "mimetypes2")
else()
  set(mime_prefix "mimetypes")
endif()

extend_qtc_library(Utils
  SOURCES_PREFIX ${mime_prefix}
  PUBLIC_INCLUDES ${mime_prefix}
  SOURCES
    mimedatabase.cpp
    mimedatabase.h
    mimedatabase_p.h
    mimeglobpattern.cpp
    mimeglobpattern_p.h
    mimemagicrule.cpp
    mimemagicrule_p.h
    mimemagicrulematcher.cpp
    mimemagicrulematcher_p.h
    mimeprovider.cpp
    mimeprovider_p.h
    mimetype.cpp
    mimetype.h
    mimetype_p.h
    mimetypeparser.cpp
    mimetypeparser_p.h
    mimeutils.cpp
)

extend_qtc_library(Utils CONDITION WIN32
  SOURCES
    touchbar/touchbar.cpp
  DEPENDS
    user32 iphlpapi ws2_32 shell32 ole32
  DEFINES
    _UNICODE UNICODE
  PUBLIC_DEFINES
    _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS
)

extend_qtc_library(Utils CONDITION APPLE
  SOURCES
    fileutils_mac.mm fileutils_mac.h
    processhandle_mac.mm
    theme/theme_mac.mm theme/theme_mac.h
    touchbar/touchbar_appdelegate_mac.mm touchbar/touchbar_appdelegate_mac_p.h
    touchbar/touchbar_mac.mm touchbar/touchbar_mac_p.h
  DEPENDS
    ${FWFoundation} ${FWAppKit}
)

extend_qtc_library(Utils CONDITION UNIX AND NOT APPLE
  SOURCES
    touchbar/touchbar.cpp
)

if (WIN32)
  add_qtc_executable(qtcreator_process_stub
    SOURCES process_stub_win.c
    DEPENDS shell32
    DEFINES _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS
  )

  add_qtc_executable(qtcreator_ctrlc_stub
    DEPENDS user32 shell32
    DEFINES _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS
    SOURCES
      process_ctrlc_stub.cpp
  )
else()
  add_qtc_executable(qtcreator_process_stub SOURCES process_stub_unix.c)
endif()
