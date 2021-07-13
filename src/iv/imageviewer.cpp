// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <iostream>
#ifndef _WIN32
#    include <unistd.h>
#endif
#include <vector>

#include "imageviewer.h"
#include "ivgl.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QResizeEvent>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/timer.h>

#include "ivutils.h"


namespace {

inline bool
IsSpecSrgb(const ImageSpec& spec)
{
    return Strutil::iequals(spec.get_string_attribute("oiio:ColorSpace"),
                            "sRGB");
}

}  // namespace


// clang-format off
static const char *s_file_filters = ""
    "Image Files (*.bmp *.cin *.dcm *.dds *.dpx *.f3d *.fits *.gif *.hdr *.ico *.iff "
    "*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi *.jp2 *.j2k *.exr *.png *.pbm *.pgm "
    "*.ppm *.psd *.ptex *.rla *.sgi *.rgb *.rgba *.bw *.int *.inta *.pic *.tga "
    "*.tpic *.tif *.tiff *.tx *.env *.sm *.vsm *.webp *.zfile);;"
    "BMP (*.bmp);;"
    "Cineon (*.cin);;"
    "Direct Draw Surface (*.dds);;"
    "DICOM (*.dcm);;"
    "DPX (*.dpx);;"
    "Field3D (*.f3d);;"
    "FITS (*.fits);;"
    "GIF (*.gif);;"
    "HDR/RGBE (*.hdr);;"
    "Icon (*.ico);;"
    "IFF (*.iff);;"
    "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi);;"
    "JPEG-2000 (*.jp2 *.j2k);;"
    "OpenEXR (*.exr);;"
    "PhotoShop (*.psd);;"
    "Portable Network Graphics (*.png);;"
    "PNM / Netpbm (*.pbm *.pgm *.ppm);;"
    "Ptex (*.ptex);;"
    "RLA (*.rla);;"
    "SGI (*.sgi *.rgb *.rgba *.bw *.int *.inta);;"
    "Softimage PIC (*.pic);;"
    "Targa (*.tga *.tpic);;"
    "TIFF (*.tif *.tiff *.tx *.env *.sm *.vsm);;"
    "Webp (*.webp);;"
    "Zfile (*.zfile);;"
    "All Files (*)";
// clang-format on



ImageViewer::ImageViewer()
    : infoWindow(NULL)
    , preferenceWindow(NULL)
    , darkPaletteBox(NULL)
    , m_current_image(-1)
    , m_current_channel(0)
    , m_color_mode(RGBA)
    , m_last_image(-1)
    , m_zoom(1)
    , m_fullscreen(false)
    , m_default_gamma(1)
    , m_darkPalette(false)
{
    readSettings(false);

    float gam = Strutil::stof(Sysutil::getenv("GAMMA"));
    if (gam >= 0.1 && gam <= 5)
        m_default_gamma = gam;
    // FIXME -- would be nice to have a more nuanced approach to display
    // color space, in particular knowing whether the display is sRGB.
    // Also, some time in the future we may want a real 3D LUT for
    // "film look", etc.

    if (darkPalette())
        m_palette = QPalette(Qt::darkGray);  // darkGray?
    else
        m_palette = QPalette();
    QApplication::setPalette(m_palette);  // FIXME -- why not work?
    this->setPalette(m_palette);

    slideTimer       = new QTimer();
    slideDuration_ms = 5000;
    slide_loop       = true;
    glwin            = new IvGL(this, *this);
    glwin->setPalette(m_palette);
    glwin->resize(m_default_width, m_default_height);
    setCentralWidget(glwin);

    createActions();
    createMenus();
    createToolBars();
    createStatusBar();

    readSettings();

    setWindowTitle(tr("Image Viewer"));
    resize(m_default_width, m_default_height);
    //    setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Ignored);
}



ImageViewer::~ImageViewer()
{
    for (auto i : m_images)
        delete i;
}



void
ImageViewer::closeEvent(QCloseEvent*)
{
    writeSettings();
}



void
ImageViewer::createActions()
{
    openAct = new QAction(tr("&Open..."), this);
    openAct->setShortcut(tr("Ctrl+O"));
    connect(openAct, SIGNAL(triggered()), this, SLOT(open()));

    for (auto& i : openRecentAct) {
        i = new QAction(this);
        i->setVisible(false);
        connect(i, SIGNAL(triggered()), this, SLOT(openRecentFile()));
    }

    reloadAct = new QAction(tr("&Reload image"), this);
    reloadAct->setShortcut(tr("Ctrl+R"));
    connect(reloadAct, SIGNAL(triggered()), this, SLOT(reload()));

    closeImgAct = new QAction(tr("&Close Image"), this);
    closeImgAct->setShortcut(tr("Ctrl+W"));
    connect(closeImgAct, SIGNAL(triggered()), this, SLOT(closeImg()));

    saveAsAct = new QAction(tr("&Save As..."), this);
    saveAsAct->setShortcut(tr("Ctrl+S"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    saveWindowAsAct = new QAction(tr("Save Window As..."), this);
    connect(saveWindowAsAct, SIGNAL(triggered()), this, SLOT(saveWindowAs()));

    saveSelectionAsAct = new QAction(tr("Save Selection As..."), this);
    connect(saveSelectionAsAct, SIGNAL(triggered()), this,
            SLOT(saveSelectionAs()));

    printAct = new QAction(tr("&Print..."), this);
    printAct->setShortcut(tr("Ctrl+P"));
    printAct->setEnabled(false);
    connect(printAct, SIGNAL(triggered()), this, SLOT(print()));

    deleteCurrentImageAct = new QAction(tr("&Delete from disk"), this);
    deleteCurrentImageAct->setShortcut(tr("Delete"));
    connect(deleteCurrentImageAct, SIGNAL(triggered()), this,
            SLOT(deleteCurrentImage()));

    editPreferencesAct = new QAction(tr("&Preferences..."), this);
    editPreferencesAct->setShortcut(tr("Ctrl+,"));
    editPreferencesAct->setEnabled(true);
    connect(editPreferencesAct, SIGNAL(triggered()), this,
            SLOT(editPreferences()));

    exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcut(tr("Ctrl+Q"));
    connect(exitAct, SIGNAL(triggered()), this, SLOT(close()));

    exposurePlusOneTenthStopAct = new QAction(tr("Exposure +1/10 stop"), this);
    exposurePlusOneTenthStopAct->setShortcut(tr("]"));
    connect(exposurePlusOneTenthStopAct, SIGNAL(triggered()), this,
            SLOT(exposurePlusOneTenthStop()));

    exposurePlusOneHalfStopAct = new QAction(tr("Exposure +1/2 stop"), this);
    exposurePlusOneHalfStopAct->setShortcut(tr("}"));
    connect(exposurePlusOneHalfStopAct, SIGNAL(triggered()), this,
            SLOT(exposurePlusOneHalfStop()));

    exposureMinusOneTenthStopAct = new QAction(tr("Exposure -1/10 stop"), this);
    exposureMinusOneTenthStopAct->setShortcut(tr("["));
    connect(exposureMinusOneTenthStopAct, SIGNAL(triggered()), this,
            SLOT(exposureMinusOneTenthStop()));

    exposureMinusOneHalfStopAct = new QAction(tr("Exposure -1/2 stop"), this);
    exposureMinusOneHalfStopAct->setShortcut(tr("{"));
    connect(exposureMinusOneHalfStopAct, SIGNAL(triggered()), this,
            SLOT(exposureMinusOneHalfStop()));

    gammaPlusAct = new QAction(tr("Gamma +0.1"), this);
    gammaPlusAct->setShortcut(tr(")"));
    connect(gammaPlusAct, SIGNAL(triggered()), this, SLOT(gammaPlus()));

    gammaMinusAct = new QAction(tr("Gamma -0.1"), this);
    gammaMinusAct->setShortcut(tr("("));
    connect(gammaMinusAct, SIGNAL(triggered()), this, SLOT(gammaMinus()));

    viewChannelFullAct = new QAction(tr("Full Color"), this);
    viewChannelFullAct->setShortcut(tr("c"));
    viewChannelFullAct->setCheckable(true);
    viewChannelFullAct->setChecked(true);
    connect(viewChannelFullAct, SIGNAL(triggered()), this,
            SLOT(viewChannelFull()));

    viewChannelRedAct = new QAction(tr("Red"), this);
    viewChannelRedAct->setShortcut(tr("r"));
    viewChannelRedAct->setCheckable(true);
    connect(viewChannelRedAct, SIGNAL(triggered()), this,
            SLOT(viewChannelRed()));

    viewChannelGreenAct = new QAction(tr("Green"), this);
    viewChannelGreenAct->setShortcut(tr("g"));
    viewChannelGreenAct->setCheckable(true);
    connect(viewChannelGreenAct, SIGNAL(triggered()), this,
            SLOT(viewChannelGreen()));

    viewChannelBlueAct = new QAction(tr("Blue"), this);
    viewChannelBlueAct->setShortcut(tr("b"));
    viewChannelBlueAct->setCheckable(true);
    connect(viewChannelBlueAct, SIGNAL(triggered()), this,
            SLOT(viewChannelBlue()));

    viewChannelAlphaAct = new QAction(tr("Alpha"), this);
    viewChannelAlphaAct->setShortcut(tr("a"));
    viewChannelAlphaAct->setCheckable(true);
    connect(viewChannelAlphaAct, SIGNAL(triggered()), this,
            SLOT(viewChannelAlpha()));

    viewColorLumAct = new QAction(tr("Luminance"), this);
    viewColorLumAct->setShortcut(tr("l"));
    viewColorLumAct->setCheckable(true);
    connect(viewColorLumAct, SIGNAL(triggered()), this,
            SLOT(viewChannelLuminance()));

    viewColorRGBAAct = new QAction(tr("RGBA"), this);
    //viewColorRGBAAct->setShortcut (tr("Ctrl+c"));
    viewColorRGBAAct->setCheckable(true);
    viewColorRGBAAct->setChecked(true);
    connect(viewColorRGBAAct, SIGNAL(triggered()), this, SLOT(viewColorRGBA()));

    viewColorRGBAct = new QAction(tr("RGB"), this);
    //viewColorRGBAct->setShortcut (tr("Ctrl+a"));
    viewColorRGBAct->setCheckable(true);
    connect(viewColorRGBAct, SIGNAL(triggered()), this, SLOT(viewColorRGB()));

    viewColor1ChAct = new QAction(tr("Single channel"), this);
    viewColor1ChAct->setShortcut(tr("1"));
    viewColor1ChAct->setCheckable(true);
    connect(viewColor1ChAct, SIGNAL(triggered()), this, SLOT(viewColor1Ch()));

    viewColorHeatmapAct = new QAction(tr("Single channel (Heatmap)"), this);
    viewColorHeatmapAct->setShortcut(tr("h"));
    viewColorHeatmapAct->setCheckable(true);
    connect(viewColorHeatmapAct, SIGNAL(triggered()), this,
            SLOT(viewColorHeatmap()));

    viewChannelPrevAct = new QAction(tr("Prev Channel"), this);
    viewChannelPrevAct->setShortcut(tr(","));
    connect(viewChannelPrevAct, SIGNAL(triggered()), this,
            SLOT(viewChannelPrev()));

    viewChannelNextAct = new QAction(tr("Next Channel"), this);
    viewChannelNextAct->setShortcut(tr("."));
    connect(viewChannelNextAct, SIGNAL(triggered()), this,
            SLOT(viewChannelNext()));

    viewSubimagePrevAct = new QAction(tr("Prev Subimage"), this);
    viewSubimagePrevAct->setShortcut(tr("<"));
    connect(viewSubimagePrevAct, SIGNAL(triggered()), this,
            SLOT(viewSubimagePrev()));

    viewSubimageNextAct = new QAction(tr("Next Subimage"), this);
    viewSubimageNextAct->setShortcut(tr(">"));
    connect(viewSubimageNextAct, SIGNAL(triggered()), this,
            SLOT(viewSubimageNext()));

    zoomInAct = new QAction(tr("Zoom &In"), this);
    zoomInAct->setShortcut(tr("Ctrl++"));
    connect(zoomInAct, SIGNAL(triggered()), this, SLOT(zoomIn()));

    zoomOutAct = new QAction(tr("Zoom &Out"), this);
    zoomOutAct->setShortcut(tr("Ctrl+-"));
    connect(zoomOutAct, SIGNAL(triggered()), this, SLOT(zoomOut()));

    normalSizeAct = new QAction(tr("&Normal Size (1:1)"), this);
    normalSizeAct->setShortcut(tr("Ctrl+0"));
    connect(normalSizeAct, SIGNAL(triggered()), this, SLOT(normalSize()));

    fitWindowToImageAct = new QAction(tr("&Fit Window to Image"), this);
    fitWindowToImageAct->setShortcut(tr("f"));
    connect(fitWindowToImageAct, SIGNAL(triggered()), this,
            SLOT(fitWindowToImage()));

    fitImageToWindowAct = new QAction(tr("Fit Image to Window"), this);
    //    fitImageToWindowAct->setEnabled(false);
    fitImageToWindowAct->setCheckable(true);
    fitImageToWindowAct->setShortcut(tr("Alt+f"));
    connect(fitImageToWindowAct, SIGNAL(triggered()), this,
            SLOT(fitImageToWindow()));

    fullScreenAct = new QAction(tr("Full screen"), this);
    //    fullScreenAct->setEnabled(false);
    //    fullScreenAct->setCheckable(true);
    fullScreenAct->setShortcut(tr("Ctrl+f"));
    connect(fullScreenAct, SIGNAL(triggered()), this, SLOT(fullScreenToggle()));

    aboutAct = new QAction(tr("&About"), this);
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

    prevImageAct = new QAction(tr("Previous Image"), this);
    prevImageAct->setShortcut(tr("PgUp"));
    //    prevImageAct->setEnabled(true);
    connect(prevImageAct, SIGNAL(triggered()), this, SLOT(prevImage()));

    nextImageAct = new QAction(tr("Next Image"), this);
    nextImageAct->setShortcut(tr("PgDown"));
    //    nextImageAct->setEnabled(true);
    connect(nextImageAct, SIGNAL(triggered()), this, SLOT(nextImage()));

    toggleImageAct = new QAction(tr("Toggle image"), this);
    toggleImageAct->setShortcut(tr("T"));
    //    toggleImageAct->setEnabled(true);
    connect(toggleImageAct, SIGNAL(triggered()), this, SLOT(toggleImage()));

    slideShowAct = new QAction(tr("Start Slide Show"), this);
    connect(slideShowAct, SIGNAL(triggered()), this, SLOT(slideShow()));

    slideLoopAct = new QAction(tr("Loop slide show"), this);
    slideLoopAct->setCheckable(true);
    slideLoopAct->setChecked(true);
    connect(slideLoopAct, SIGNAL(triggered()), this, SLOT(slideLoop()));

    slideNoLoopAct = new QAction(tr("Stop at end"), this);
    slideNoLoopAct->setCheckable(true);
    connect(slideNoLoopAct, SIGNAL(triggered()), this, SLOT(slideNoLoop()));

    sortByNameAct = new QAction(tr("By Name"), this);
    connect(sortByNameAct, SIGNAL(triggered()), this, SLOT(sortByName()));

    sortByPathAct = new QAction(tr("By File Path"), this);
    connect(sortByPathAct, SIGNAL(triggered()), this, SLOT(sortByPath()));

    sortByImageDateAct = new QAction(tr("By Image Date"), this);
    connect(sortByImageDateAct, SIGNAL(triggered()), this,
            SLOT(sortByImageDate()));

    sortByFileDateAct = new QAction(tr("By File Date"), this);
    connect(sortByFileDateAct, SIGNAL(triggered()), this,
            SLOT(sortByFileDate()));

    sortReverseAct = new QAction(tr("Reverse current order"), this);
    connect(sortReverseAct, SIGNAL(triggered()), this, SLOT(sortReverse()));

    showInfoWindowAct = new QAction(tr("&Image info..."), this);
    showInfoWindowAct->setShortcut(tr("Ctrl+I"));
    //    showInfoWindowAct->setEnabled(true);
    connect(showInfoWindowAct, SIGNAL(triggered()), this,
            SLOT(showInfoWindow()));

    showPixelviewWindowAct = new QAction(tr("&Pixel closeup view..."), this);
    showPixelviewWindowAct->setCheckable(true);
    showPixelviewWindowAct->setShortcut(tr("P"));
    connect(showPixelviewWindowAct, SIGNAL(triggered()), this,
            SLOT(showPixelviewWindow()));

    pixelviewFollowsMouseBox = new QCheckBox(tr("Pixel view follows mouse"));
    pixelviewFollowsMouseBox->setChecked(false);
    linearInterpolationBox = new QCheckBox(tr("Linear interpolation"));
    linearInterpolationBox->setChecked(true);
    darkPaletteBox = new QCheckBox(tr("Dark palette"));
    darkPaletteBox->setChecked(true);
    autoMipmap = new QCheckBox(tr("Generate mipmaps (requires restart)"));
    autoMipmap->setChecked(false);

    maxMemoryICLabel = new QLabel(
        tr("Image Cache max memory (requires restart)"));
    maxMemoryIC = new QSpinBox();
    if (sizeof(void*) == 4)
        maxMemoryIC->setRange(128, 2048);  //2GB is enough for 32 bit machines
    else
        maxMemoryIC->setRange(128, 8192);  //8GB probably ok for 64 bit
    maxMemoryIC->setSingleStep(64);
    maxMemoryIC->setSuffix(" MB");

    slideShowDurationLabel = new QLabel(tr("Slide Show delay"));
    slideShowDuration      = new QSpinBox();
    slideShowDuration->setRange(1, 3600);
    slideShowDuration->setSingleStep(1);
    slideShowDuration->setSuffix(" s");
    slideShowDuration->setAccelerated(true);
    connect(slideShowDuration, SIGNAL(valueChanged(int)), this,
            SLOT(setSlideShowDuration(int)));
}



void
ImageViewer::createMenus()
{
    openRecentMenu = new QMenu(tr("Open recent..."), this);
    for (auto& i : openRecentAct)
        openRecentMenu->addAction(i);

    fileMenu = new QMenu(tr("&File"), this);
    fileMenu->addAction(openAct);
    fileMenu->addMenu(openRecentMenu);
    fileMenu->addAction(reloadAct);
    fileMenu->addAction(closeImgAct);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAsAct);
    fileMenu->addAction(saveWindowAsAct);
    fileMenu->addAction(saveSelectionAsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(printAct);
    fileMenu->addAction(deleteCurrentImageAct);
    fileMenu->addSeparator();
    fileMenu->addAction(editPreferencesAct);
    fileMenu->addAction(exitAct);
    menuBar()->addMenu(fileMenu);

    // Copy
    // Paste
    // Clear selection
    // radio: prioritize selection, crop selection

    expgamMenu = new QMenu(tr("Exposure/gamma"));  // submenu
    expgamMenu->addAction(exposureMinusOneHalfStopAct);
    expgamMenu->addAction(exposureMinusOneTenthStopAct);
    expgamMenu->addAction(exposurePlusOneHalfStopAct);
    expgamMenu->addAction(exposurePlusOneTenthStopAct);
    expgamMenu->addAction(gammaMinusAct);
    expgamMenu->addAction(gammaPlusAct);

    //    imageMenu = new QMenu(tr("&Image"), this);
    //    menuBar()->addMenu (imageMenu);
    slideMenu = new QMenu(tr("Slide Show"));
    slideMenu->addAction(slideShowAct);
    slideMenu->addAction(slideLoopAct);
    slideMenu->addAction(slideNoLoopAct);

    sortMenu = new QMenu(tr("Sort"));
    sortMenu->addAction(sortByNameAct);
    sortMenu->addAction(sortByPathAct);
    sortMenu->addAction(sortByImageDateAct);
    sortMenu->addAction(sortByFileDateAct);
    sortMenu->addAction(sortReverseAct);

    channelMenu = new QMenu(tr("Channels"));
    // Color mode: true, random, falsegrgbacCrgR
    channelMenu->addAction(viewChannelFullAct);
    channelMenu->addAction(viewChannelRedAct);
    channelMenu->addAction(viewChannelGreenAct);
    channelMenu->addAction(viewChannelBlueAct);
    channelMenu->addAction(viewChannelAlphaAct);
    channelMenu->addAction(viewChannelPrevAct);
    channelMenu->addAction(viewChannelNextAct);

    colormodeMenu = new QMenu(tr("Color mode"));
    colormodeMenu->addAction(viewColorRGBAAct);
    colormodeMenu->addAction(viewColorRGBAct);
    colormodeMenu->addAction(viewColor1ChAct);
    colormodeMenu->addAction(viewColorLumAct);
    colormodeMenu->addAction(viewColorHeatmapAct);

    viewMenu = new QMenu(tr("&View"), this);
    viewMenu->addAction(prevImageAct);
    viewMenu->addAction(nextImageAct);
    viewMenu->addAction(toggleImageAct);
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAct);
    viewMenu->addAction(zoomOutAct);
    viewMenu->addAction(normalSizeAct);
    viewMenu->addAction(fitWindowToImageAct);
    viewMenu->addAction(fitImageToWindowAct);
    viewMenu->addAction(fullScreenAct);
    viewMenu->addSeparator();
    viewMenu->addAction(viewSubimagePrevAct);
    viewMenu->addAction(viewSubimageNextAct);
    viewMenu->addMenu(channelMenu);
    viewMenu->addMenu(colormodeMenu);
    viewMenu->addMenu(expgamMenu);
    menuBar()->addMenu(viewMenu);
    // Full screen mode
    // prev subimage <, next subimage >
    // fg/bg image...

    toolsMenu = new QMenu(tr("&Tools"), this);
    // Mode: select, zoom, pan, wipe
    toolsMenu->addAction(showInfoWindowAct);
    toolsMenu->addAction(showPixelviewWindowAct);
    toolsMenu->addMenu(slideMenu);
    toolsMenu->addMenu(sortMenu);

    // Menus, toolbars, & status
    // Annotate
    // [check] overwrite render
    // connect renderer
    // kill renderer
    // store render
    // Playback: forward, reverse, faster, slower, loop/pingpong
    menuBar()->addMenu(toolsMenu);

    helpMenu = new QMenu(tr("&Help"), this);
    helpMenu->addAction(aboutAct);
    menuBar()->addMenu(helpMenu);
    // Bring up user's guide
}



void
ImageViewer::createToolBars()
{
#if 0
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->addAction(newAct);
    fileToolBar->addAction(openAct);
    fileToolBar->addAction(saveAct);

    editToolBar = addToolBar(tr("Edit"));
    editToolBar->addAction(cutAct);
    editToolBar->addAction(copyAct);
    editToolBar->addAction(pasteAct);
#endif
}



void
ImageViewer::createStatusBar()
{
    statusImgInfo = new QLabel;
    statusBar()->addWidget(statusImgInfo);

    statusViewInfo = new QLabel;
    statusBar()->addWidget(statusViewInfo);

    statusProgress = new QProgressBar;
    statusProgress->setRange(0, 100);
    statusProgress->reset();
    statusBar()->addWidget(statusProgress);

    mouseModeComboBox = new QComboBox;
    mouseModeComboBox->addItem(tr("Zoom"));
    mouseModeComboBox->addItem(tr("Pan"));
    mouseModeComboBox->addItem(tr("Wipe"));
    mouseModeComboBox->addItem(tr("Select"));
    mouseModeComboBox->addItem(tr("Annotate"));
    // Note: the order of the above MUST match the order of enum MouseMode
    statusBar()->addWidget(mouseModeComboBox);
    mouseModeComboBox->hide();
}



void
ImageViewer::readSettings(bool ui_is_set_up)
{
    QSettings settings("OpenImageIO", "iv");
    m_darkPalette = settings.value("darkPalette").toBool();
    if (!ui_is_set_up)
        return;
    pixelviewFollowsMouseBox->setChecked(
        settings.value("pixelviewFollowsMouse").toBool());
    linearInterpolationBox->setChecked(
        settings.value("linearInterpolation").toBool());
    darkPaletteBox->setChecked(settings.value("darkPalette").toBool());
    QStringList recent = settings.value("RecentFiles").toStringList();
    for (auto&& s : recent)
        addRecentFile(s.toStdString());
    updateRecentFilesMenu();  // only safe because it's called after menu setup

    autoMipmap->setChecked(settings.value("autoMipmap", false).toBool());
    if (sizeof(void*) == 4)  // 32 bit or 64?
        maxMemoryIC->setValue(settings.value("maxMemoryIC", 512).toInt());
    else
        maxMemoryIC->setValue(settings.value("maxMemoryIC", 2048).toInt());
    slideShowDuration->setValue(
        settings.value("slideShowDuration", 10).toInt());

    ImageCache* imagecache = ImageCache::create(true);
    imagecache->attribute("automip", autoMipmap->isChecked());
    imagecache->attribute("max_memory_MB", (float)maxMemoryIC->value());
}



void
ImageViewer::writeSettings()
{
    QSettings settings("OpenImageIO", "iv");
    settings.setValue("pixelviewFollowsMouse",
                      pixelviewFollowsMouseBox->isChecked());
    settings.setValue("linearInterpolation",
                      linearInterpolationBox->isChecked());
    settings.setValue("darkPalette", darkPaletteBox->isChecked());
    settings.setValue("autoMipmap", autoMipmap->isChecked());
    settings.setValue("maxMemoryIC", maxMemoryIC->value());
    settings.setValue("slideShowDuration", slideShowDuration->value());
    QStringList recent;
    for (auto&& s : m_recent_files)
        recent.push_front(QString(s.c_str()));
    settings.setValue("RecentFiles", recent);
}



bool
image_progress_callback(void* opaque, float done)
{
    ImageViewer* viewer = (ImageViewer*)opaque;
    viewer->statusProgress->setValue((int)(done * 100));
    QApplication::processEvents();
    return false;
}



void
ImageViewer::open()
{
    static QString openPath = QDir::currentPath();
    QFileDialog dialog(NULL, tr("Open File(s)"), openPath, tr(s_file_filters));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    if (!dialog.exec())
        return;
    openPath          = dialog.directory().path();
    QStringList names = dialog.selectedFiles();

    int old_lastimage = m_images.size() - 1;
    for (auto& name : names) {
        std::string filename = name.toUtf8().data();
        if (filename.empty())
            continue;
        add_image(filename);
        //        int n = m_images.size()-1;
        //        IvImage *newimage = m_images[n];
        //        newimage->read_iv (0, false, image_progress_callback, this);
    }
    if (old_lastimage >= 0) {
        // Otherwise, add_image already did this for us.
        current_image(old_lastimage + 1);
        fitWindowToImage(true, true);
    }
}



void
ImageViewer::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        std::string filename = action->data().toString().toStdString();
        // If it's an image we already have loaded, just switch to it
        // (and reload) rather than loading a second copy.
        for (size_t i = 0; i < m_images.size(); ++i) {
            if (m_images[i]->name() == filename) {
                current_image(i);
                reload();
                return;
            }
        }
        // It's not an image we already have loaded
        add_image(filename);
        if (m_images.size() > 1) {
            // Otherwise, add_image already did this for us.
            current_image(m_images.size() - 1);
            fitWindowToImage(true, true);
        }
    }
}



void
ImageViewer::addRecentFile(const std::string& name)
{
    removeRecentFile(name);
    m_recent_files.insert(m_recent_files.begin(), name);
    if (m_recent_files.size() > MaxRecentFiles)
        m_recent_files.resize(MaxRecentFiles);
}



void
ImageViewer::removeRecentFile(const std::string& name)
{
    for (size_t i = 0; i < m_recent_files.size(); ++i) {
        if (m_recent_files[i] == name) {
            m_recent_files.erase(m_recent_files.begin() + i);
            --i;
        }
    }
}



void
ImageViewer::updateRecentFilesMenu()
{
    for (size_t i = 0; i < MaxRecentFiles; ++i) {
        if (i < m_recent_files.size()) {
            std::string name = Filesystem::filename(m_recent_files[i]);
            openRecentAct[i]->setText(QString::fromStdString(name));
            openRecentAct[i]->setData(m_recent_files[i].c_str());
            openRecentAct[i]->setVisible(true);
        } else {
            openRecentAct[i]->setVisible(false);
        }
    }
}



void
ImageViewer::reload()
{
    if (m_images.empty())
        return;
    IvImage* newimage = m_images[m_current_image];
    newimage->invalidate();
    //glwin->trigger_redraw ();
    displayCurrentImage();
}



void
ImageViewer::add_image(const std::string& filename)
{
    if (filename.empty())
        return;
    IvImage* newimage = nullptr;
    if (rawcolor()) {
        ImageSpec config;
        config.attribute("oiio:RawColor", 1);
        newimage = new IvImage(filename, &config);
    } else {
        newimage = new IvImage(filename);
    }
    newimage->gamma(m_default_gamma);
    m_images.push_back(newimage);
    addRecentFile(filename);
    updateRecentFilesMenu();

#if 0
    if (getspec) {
        if (! newimage->init_spec (filename)) {
            QMessageBox::information (this, tr("iv Image Viewer"),
                              tr("%1").arg(newimage->geterror().c_str()));
        } else {
            std::cerr << "Added image " << filename << ": " 
<< newimage->spec().width << " x " << newimage->spec().height << "\n";
        }
        return;
    }
#endif
    if (m_images.size() == 1) {
        // If this is the first image, resize to fit it
        displayCurrentImage();
        fitWindowToImage(true, true);
    }
}



void
ImageViewer::saveAs()
{
    IvImage* img = cur();
    if (!img)
        return;
    QString name;
    name = QFileDialog::getSaveFileName(this, tr("Save Image"),
                                        QString(img->name().c_str()),
                                        tr(s_file_filters));
    if (name.isEmpty())
        return;
    bool ok = img->write(name.toStdString(), TypeUnknown, "",
                         image_progress_callback, this);
    if (!ok) {
        std::cerr << "Save failed: " << img->geterror() << "\n";
    }
}



void
ImageViewer::saveWindowAs()
{
    IvImage* img = cur();
    if (!img)
        return;
    QString name;
    name = QFileDialog::getSaveFileName(this, tr("Save Window"),
                                        QString(img->name().c_str()));
    if (name.isEmpty())
        return;
    img->write(name.toStdString(), TypeUnknown, "", image_progress_callback,
               this);
}



void
ImageViewer::saveSelectionAs()
{
    IvImage* img = cur();
    if (!img)
        return;
    QString name;
    name = QFileDialog::getSaveFileName(this, tr("Save Selection"),
                                        QString(img->name().c_str()));
    if (name.isEmpty())
        return;
    img->write(name.toStdString(), TypeUnknown, "", image_progress_callback,
               this);
}



void
ImageViewer::updateTitle()
{
    IvImage* img = cur();
    if (!img) {
        setWindowTitle(tr("iv Image Viewer (no image loaded)"));
        return;
    }
    std::string message;
    message = Strutil::sprintf("%s - iv Image Viewer", img->name().c_str());
    setWindowTitle(QString::fromLocal8Bit(message.c_str()));
}



void
ImageViewer::updateStatusBar()
{
    const ImageSpec* spec = curspec();
    if (!spec) {
        statusImgInfo->setText(tr("No image loaded"));
        statusViewInfo->setText(tr(""));
        return;
    }
    std::string message;
    message = Strutil::sprintf("(%d/%d) : ", m_current_image + 1,
                               (int)m_images.size());
    message += cur()->shortinfo();
    statusImgInfo->setText(message.c_str());

    message.clear();
    switch (m_color_mode) {
    case RGBA:
        message = Strutil::sprintf("RGBA (%d-%d)", m_current_channel,
                                   m_current_channel + 3);
        break;
    case RGB:
        message = Strutil::sprintf("RGB (%d-%d)", m_current_channel,
                                   m_current_channel + 2);
        break;
    case LUMINANCE:
        message = Strutil::sprintf("Lum (%d-%d)", m_current_channel,
                                   m_current_channel + 2);
        break;
    case HEATMAP: message = "Heat ";
    case SINGLE_CHANNEL:
        if ((int)spec->channelnames.size() > m_current_channel
            && spec->channelnames[m_current_channel].size())
            message += spec->channelnames[m_current_channel];
        else if (m_color_mode == HEATMAP) {
            message += Strutil::sprintf("%d", m_current_channel);
        } else {
            message = Strutil::sprintf("chan %d", m_current_channel);
        }
        break;
    }
    message += Strutil::sprintf("  %g:%g  exp %+.1f  gam %.2f",
                                zoom() >= 1 ? zoom() : 1.0f,
                                zoom() >= 1 ? 1.0f : 1.0f / zoom(),
                                cur()->exposure(), cur()->gamma());
    if (cur()->nsubimages() > 1) {
        if (cur()->auto_subimage()) {
            message += Strutil::sprintf("  subimg AUTO (%d/%d)",
                                        cur()->subimage() + 1,
                                        cur()->nsubimages());
        } else {
            message += Strutil::sprintf("  subimg %d/%d", cur()->subimage() + 1,
                                        cur()->nsubimages());
        }
    }
    if (cur()->nmiplevels() > 1) {
        message += Strutil::sprintf("  MIP %d/%d", cur()->miplevel() + 1,
                                    cur()->nmiplevels());
    }

    statusViewInfo->setText(message.c_str());  // tr("iv status"));
}



bool
ImageViewer::loadCurrentImage(int subimage, int miplevel)
{
    if (m_current_image < 0 || m_current_image >= (int)m_images.size()) {
        m_current_image = 0;
    }
    IvImage* img = cur();
    if (img) {
        // We need the spec available to compare the image format with
        // opengl's capabilities.
        if (!img->init_spec(img->name(), subimage, miplevel)) {
            statusImgInfo->setText(
                tr("Could not display image: %1.").arg(img->name().c_str()));
            statusViewInfo->setText(tr(""));
            return false;
        }

        // Used to check whether we'll need to do adjustments in the
        // CPU. If true, images should be loaded as UINT8.
        bool allow_transforms = false;
        bool srgb_transform   = false;

        // By default, we try to load into OpenGL with the same format,
        TypeDesc read_format        = TypeDesc::UNKNOWN;
        const ImageSpec& image_spec = img->spec();

        if (image_spec.format.basetype == TypeDesc::DOUBLE) {
            // AFAIK, OpenGL doesn't support 64-bit floats as pixel size.
            read_format = TypeDesc::FLOAT;
        }
        if (glwin->is_glsl_capable()) {
            if (image_spec.format.basetype == TypeDesc::HALF
                && !glwin->is_half_capable()) {
                //std::cerr << "Loading HALF-FLOAT as FLOAT\n";
                read_format = TypeDesc::FLOAT;
            }
            if (IsSpecSrgb(image_spec) && !glwin->is_srgb_capable()) {
                // If the image is in sRGB, but OpenGL can't load sRGB textures then
                // we'll need to do the transformation on the CPU after loading the
                // image. We (so far) can only do this with UINT8 images, so make
                // sure that it gets loaded in this format.
                //std::cerr << "Loading as UINT8 to do sRGB\n";
                read_format      = TypeDesc::UINT8;
                srgb_transform   = true;
                allow_transforms = true;
            }
        } else {
            //std::cerr << "Loading as UINT8\n";
            read_format      = TypeDesc::UINT8;
            allow_transforms = true;

            if (IsSpecSrgb(image_spec) && !glwin->is_srgb_capable())
                srgb_transform = true;
        }

        // FIXME: This actually won't work since the ImageCacheFile has already
        // been created when we did the init_spec.
        // Check whether IvGL recommends generating mipmaps for this image.
        //ImageCache *imagecache = ImageCache::create (true);
        //if (glwin->is_too_big (img) && autoMipmap->isChecked ()) {
        //    imagecache->attribute ("automip", 1);
        //} else {
        //    imagecache->attribute ("automip", 0);
        //}

        // Read the image from disk or from the ImageCache if available.
        if (img->read_iv(subimage, miplevel, false, read_format,
                         image_progress_callback, this, allow_transforms)) {
            // The image was read successfully.
            // Check if we've got to do sRGB to linear (ie, when not supported
            // by OpenGL).
            // Do the first pixel transform to fill-in the secondary image
            // buffer.
            if (allow_transforms) {
                img->pixel_transform(srgb_transform, (int)current_color_mode(),
                                     current_channel());
            }
            return true;
        } else {
            statusImgInfo->setText(
                tr("Could not display image: %1.").arg(img->name().c_str()));
            statusViewInfo->setText(tr(""));
            return false;
        }
    }
    return false;
}



void
ImageViewer::displayCurrentImage(bool update)
{
    if (m_current_image < 0 || m_current_image >= (int)m_images.size())
        m_current_image = 0;
    IvImage* img = cur();
    if (img) {
        if (!img->image_valid()) {
            bool load_result = false;

            statusViewInfo->hide();
            statusProgress->show();
            load_result = loadCurrentImage(std::max(0, img->subimage()),
                                           std::max(0, img->miplevel()));
            statusProgress->hide();
            statusViewInfo->show();

            if (load_result) {
                update = true;
            } else {
                return;
            }
        }
    } else {
        m_current_image = m_last_image = -1;
        ((QOpenGLWidget*)(glwin))->update();
    }

    if (update) {
        glwin->update();
    }
    float z = zoom();
    if (fitImageToWindowAct->isChecked())
        z = zoom_needed_to_fit(glwin->width(), glwin->height());
    zoom(z);
    //    glwin->trigger_redraw ();

    updateTitle();
    updateStatusBar();
    if (infoWindow)
        infoWindow->update(img);

    //    printAct->setEnabled(true);
    //    fitImageToWindowAct->setEnabled(true);
    //    fullScreenAct->setEnabled(true);
    updateActions();
}



void
ImageViewer::deleteCurrentImage()
{
    IvImage* img = cur();
    if (img) {
        const char* filename = img->name().c_str();
        QString message("Are you sure you want to remove <b>");
        message = message + QString(filename) + QString("</b> file from disk?");
        QMessageBox::StandardButton button;
        button = QMessageBox::question(this, "", message,
                                       QMessageBox::Yes | QMessageBox::No);
        if (button == QMessageBox::Yes) {
            closeImg();
            int r = remove(filename);
            if (r)
                QMessageBox::information(this, "", "Unable to delete file");
        }
    }
}



void
ImageViewer::current_image(int newimage)
{
#ifndef NDEBUG
    Timer swap_image_time;
    swap_image_time.start();
#endif
    if (m_images.empty() || newimage < 0 || newimage >= (int)m_images.size())
        m_current_image = 0;
    if (m_current_image != newimage) {
        m_last_image    = (m_current_image >= 0) ? m_current_image : newimage;
        m_current_image = newimage;
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
#ifndef NDEBUG
    swap_image_time.stop();
    std::cerr << "Current Image change elapsed time: " << swap_image_time()
              << " seconds \n";
#endif
}



void
ImageViewer::prevImage()
{
    if (m_images.empty())
        return;
    if (m_current_image == 0)
        current_image((int)m_images.size() - 1);
    else
        current_image(current_image() - 1);
}


void
ImageViewer::nextImage()
{
    if (m_images.empty())
        return;
    if (m_current_image >= (int)m_images.size() - 1)
        current_image(0);
    else
        current_image(current_image() + 1);
}



void
ImageViewer::toggleImage()
{
    current_image(m_last_image);
}



void
ImageViewer::exposureMinusOneTenthStop()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->exposure(img->exposure() - 0.1);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}


void
ImageViewer::exposureMinusOneHalfStop()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->exposure(img->exposure() - 0.5);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}


void
ImageViewer::exposurePlusOneTenthStop()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->exposure(img->exposure() + 0.1);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}


void
ImageViewer::exposurePlusOneHalfStop()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->exposure(img->exposure() + 0.5);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}



void
ImageViewer::gammaMinus()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->gamma(img->gamma() - 0.05);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}


void
ImageViewer::gammaPlus()
{
    if (m_images.empty())
        return;
    IvImage* img = m_images[m_current_image];
    img->gamma(img->gamma() + 0.05);
    if (!glwin->is_glsl_capable()) {
        bool srgb_transform = (!glwin->is_srgb_capable()
                               && IsSpecSrgb(img->spec()));
        img->pixel_transform(srgb_transform, (int)current_color_mode(),
                             current_channel());
        displayCurrentImage();
    } else {
        displayCurrentImage(false);
    }
}



void
ImageViewer::slide(long /*t*/, bool b)
{
    slideLoopAct->setChecked(b == true);
    slideNoLoopAct->setChecked(b == false);
}



void
ImageViewer::viewChannel(int c, COLOR_MODE colormode)
{
#ifndef NDEBUG
    Timer change_channel_time;
    change_channel_time.start();
#endif
    if (m_current_channel != c || colormode != m_color_mode) {
        bool update = true;
        if (!glwin->is_glsl_capable()) {
            IvImage* img = cur();
            if (img) {
                bool srgb_transform = (!glwin->is_srgb_capable()
                                       && IsSpecSrgb(img->spec()));
                img->pixel_transform(srgb_transform, (int)colormode, c);
            }
        } else {
            // FIXME: There are even more chances to avoid updating the textures
            // if we can keep trac of which channels are in the texture.
            if (m_current_channel == c) {
                if (m_color_mode == SINGLE_CHANNEL || m_color_mode == HEATMAP) {
                    if (colormode == HEATMAP || colormode == SINGLE_CHANNEL)
                        update = false;
                } else if (m_color_mode == RGB || m_color_mode == LUMINANCE) {
                    if (colormode == RGB || colormode == LUMINANCE)
                        update = false;
                }
            }
        }
        m_current_channel = c;
        m_color_mode      = colormode;
        displayCurrentImage(update);

        viewChannelFullAct->setChecked(c == 0 && m_color_mode == RGBA);
        viewChannelRedAct->setChecked(c == 0 && m_color_mode == SINGLE_CHANNEL);
        viewChannelGreenAct->setChecked(c == 1
                                        && m_color_mode == SINGLE_CHANNEL);
        viewChannelBlueAct->setChecked(c == 2
                                       && m_color_mode == SINGLE_CHANNEL);
        viewChannelAlphaAct->setChecked(c == 3
                                        && m_color_mode == SINGLE_CHANNEL);
        viewColorLumAct->setChecked(m_color_mode == LUMINANCE);
        viewColorRGBAAct->setChecked(m_color_mode == RGBA);
        viewColorRGBAct->setChecked(m_color_mode == RGB);
        viewColor1ChAct->setChecked(m_color_mode == SINGLE_CHANNEL);
        viewColorHeatmapAct->setChecked(m_color_mode == HEATMAP);
    }
#ifndef NDEBUG
    change_channel_time.stop();
    std::cerr << "Change channel elapsed time: " << change_channel_time()
              << " seconds \n";
#endif
}


void
ImageViewer::slideImages()
{
    if (m_images.empty())
        return;
    if (m_current_image >= (int)m_images.size() - 1) {
        if (slide_loop == true)
            current_image(0);
        else {
            slideTimer->stop();
            disconnect(slideTimer, 0, 0, 0);
        }
    } else
        current_image(current_image() + 1);
}


void
ImageViewer::slideShow()
{
    fullScreenToggle();
    connect(slideTimer, SIGNAL(timeout()), this, SLOT(slideImages()));
    slideTimer->start(slideDuration_ms);
    updateActions();
}



void
ImageViewer::slideLoop()
{
    slide_loop = true;
    slide(slideDuration_ms, slide_loop);
}


void
ImageViewer::slideNoLoop()
{
    slide_loop = false;
    slide(slideDuration_ms, slide_loop);
}


void
ImageViewer::setSlideShowDuration(int seconds)
{
    slideDuration_ms = seconds * 1000;
}



static bool
compName(IvImage* first, IvImage* second)
{
    std::string firstFile  = Filesystem::filename(first->name());
    std::string secondFile = Filesystem::filename(second->name());
    return (firstFile.compare(secondFile) < 0);
}



void
ImageViewer::sortByName()
{
    int numImg = m_images.size();
    if (numImg < 2)
        return;
    std::sort(m_images.begin(), m_images.end(), &compName);
    current_image(0);
    displayCurrentImage();
    // updateActions();
}



static bool
compPath(IvImage* first, IvImage* second)
{
    std::string firstFile  = first->name();
    std::string secondFile = second->name();
    return (firstFile.compare(secondFile) < 0);
}



void
ImageViewer::sortByPath()
{
    int numImg = m_images.size();
    if (numImg < 2)
        return;
    std::sort(m_images.begin(), m_images.end(), &compPath);
    current_image(0);
    displayCurrentImage();
    // updateActions();
}



static bool
DateTime_to_time_t(const char* datetime, time_t& timet)
{
    int year, month, day, hour, min, sec;
    int r = sscanf(datetime, "%d:%d:%d %d:%d:%d", &year, &month, &day, &hour,
                   &min, &sec);
    // printf ("%d  %d:%d:%d %d:%d:%d\n", r, year, month, day, hour, min, sec);
    if (r != 6)
        return false;
    struct tm tmtime;
    time_t now;
    Sysutil::get_local_time(&now, &tmtime);  // fill in defaults
    tmtime.tm_sec  = sec;
    tmtime.tm_min  = min;
    tmtime.tm_hour = hour;
    tmtime.tm_mday = day;
    tmtime.tm_mon  = month - 1;
    tmtime.tm_year = year - 1900;
    timet          = mktime(&tmtime);
    return true;
}



static bool
compImageDate(IvImage* first, IvImage* second)
{
    std::time_t firstFile  = time(NULL);
    std::time_t secondFile = time(NULL);
    double diff;
    std::string metadatatime = first->spec().get_string_attribute("DateTime");
    if (metadatatime.empty()) {
        if (first->init_spec(first->name(), 0, 0)) {
            metadatatime = first->spec().get_string_attribute("DateTime");
            if (metadatatime.empty()) {
                if (!Filesystem::exists(first->name()))
                    return false;
                firstFile = Filesystem::last_write_time(first->name());
            }
        } else
            return false;
    }
    DateTime_to_time_t(metadatatime.c_str(), firstFile);
    metadatatime = second->spec().get_string_attribute("DateTime");
    if (metadatatime.empty()) {
        if (second->init_spec(second->name(), 0, 0)) {
            metadatatime = second->spec().get_string_attribute("DateTime");
            if (metadatatime.empty()) {
                if (!Filesystem::exists(second->name()))
                    return true;
                secondFile = Filesystem::last_write_time(second->name());
            }
        } else
            return true;
    }
    DateTime_to_time_t(metadatatime.c_str(), secondFile);
    diff = difftime(firstFile, secondFile);
    if (diff == 0)
        return compName(first, second);
    return (diff < 0);
}



void
ImageViewer::sortByImageDate()
{
    int numImg = m_images.size();
    if (numImg < 2)
        return;
    std::sort(m_images.begin(), m_images.end(), &compImageDate);
    current_image(0);
    displayCurrentImage();
    // updateActions();
}



static bool
compFileDate(IvImage* first, IvImage* second)
{
    std::time_t firstFile, secondFile;
    double diff;
    if (!Filesystem::exists(first->name()))
        return false;
    firstFile = Filesystem::last_write_time(first->name());
    if (!Filesystem::exists(second->name()))
        return true;
    secondFile = Filesystem::last_write_time(second->name());
    diff       = difftime(firstFile, secondFile);
    if (diff == 0)
        return compName(first, second);
    return (diff < 0);
}



void
ImageViewer::sortByFileDate()
{
    int numImg = m_images.size();
    if (numImg < 2)
        return;
    std::sort(m_images.begin(), m_images.end(), &compFileDate);
    current_image(0);
    displayCurrentImage();
    // updateActions();
}



void
ImageViewer::sortReverse()
{
    int numImg = m_images.size();
    if (numImg < 2)
        return;
    std::reverse(m_images.begin(), m_images.end());
    current_image(0);
    displayCurrentImage();
    // updateActions();
}



void
ImageViewer::viewChannelFull()
{
    viewChannel(0, RGBA);
}


void
ImageViewer::viewChannelRed()
{
    viewChannel(0, SINGLE_CHANNEL);
}


void
ImageViewer::viewChannelGreen()
{
    viewChannel(1, SINGLE_CHANNEL);
}


void
ImageViewer::viewChannelBlue()
{
    viewChannel(2, SINGLE_CHANNEL);
}


void
ImageViewer::viewChannelAlpha()
{
    viewChannel(3, SINGLE_CHANNEL);
}


void
ImageViewer::viewChannelLuminance()
{
    viewChannel(m_current_channel, LUMINANCE);
}


void
ImageViewer::viewColorRGBA()
{
    viewChannel(m_current_channel, RGBA);
}


void
ImageViewer::viewColorRGB()
{
    viewChannel(m_current_channel, RGB);
}


void
ImageViewer::viewColor1Ch()
{
    viewChannel(m_current_channel, SINGLE_CHANNEL);
}


void
ImageViewer::viewColorHeatmap()
{
    viewChannel(m_current_channel, HEATMAP);
}


void
ImageViewer::viewChannelPrev()
{
    if (glwin->is_glsl_capable()) {
        if (m_current_channel > 0)
            viewChannel(m_current_channel - 1, m_color_mode);
    } else {
        // Simulate old behavior.
        if (m_color_mode == RGBA || m_color_mode == RGB) {
            viewChannel(m_current_channel, LUMINANCE);
        } else if (m_color_mode == SINGLE_CHANNEL) {
            if (m_current_channel == 0)
                viewChannelFull();
            else
                viewChannel(m_current_channel - 1, SINGLE_CHANNEL);
        }
    }
}


void
ImageViewer::viewChannelNext()
{
    if (glwin->is_glsl_capable()) {
        viewChannel(m_current_channel + 1, m_color_mode);
    } else {
        // Simulate old behavior.
        if (m_color_mode == LUMINANCE) {
            viewChannelFull();
        } else if (m_color_mode == RGBA || m_color_mode == RGB) {
            viewChannelRed();
        } else if (m_color_mode == SINGLE_CHANNEL) {
            viewChannel(m_current_channel + 1, SINGLE_CHANNEL);
        }
    }
}



void
ImageViewer::viewSubimagePrev()
{
    IvImage* img = cur();
    if (!img)
        return;
    bool ok = false;
    if (img->miplevel() > 0) {
        ok = loadCurrentImage(img->subimage(), img->miplevel() - 1);
    } else if (img->subimage() > 0) {
        ok = loadCurrentImage(img->subimage() - 1);
    } else if (img->nsubimages() > 0) {
        img->auto_subimage(true);
        ok = loadCurrentImage(0);
    }
    if (ok) {
        if (fitImageToWindowAct->isChecked())
            fitImageToWindow();
        displayCurrentImage();
    }
}


void
ImageViewer::viewSubimageNext()
{
    IvImage* img = cur();
    if (!img)
        return;
    bool ok = false;
    if (img->auto_subimage()) {
        img->auto_subimage(false);
        ok = loadCurrentImage(0);
    } else if (img->miplevel() < img->nmiplevels() - 1) {
        ok = loadCurrentImage(img->subimage(), img->miplevel() + 1);
    } else if (img->subimage() < img->nsubimages() - 1) {
        ok = loadCurrentImage(img->subimage() + 1);
    }
    if (ok) {
        if (fitImageToWindowAct->isChecked())
            fitImageToWindow();
        displayCurrentImage();
    }
}



void
ImageViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp: prevImage(); return;  //break;
    case Qt::Key_Right:
        //        std::cerr << "Modifier is " << (int)event->modifiers() << '\n';
        //        fprintf (stderr, "%x\n", (int)event->modifiers());
        //        if (event->modifiers() & Qt::ShiftModifier)
        //            std::cerr << "hey, ctrl right\n";
    case Qt::Key_Down:
    case Qt::Key_PageDown: nextImage(); return;  //break;
    case Qt::Key_Escape:
        if (m_fullscreen)
            fullScreenToggle();
        return;
    case Qt::Key_Minus:
    case Qt::Key_Underscore: zoomOut(); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomIn(); break;
    default:
        // std::cerr << "ImageViewer key " << (int)event->key() << '\n';
        QMainWindow::keyPressEvent(event);
    }
}



void
ImageViewer::resizeEvent(QResizeEvent* event)
{
    if (fitImageToWindowAct->isChecked())
        fitImageToWindow();
    QMainWindow::resizeEvent(event);
}



void
ImageViewer::closeImg()
{
    if (m_images.empty())
        return;
    delete m_images[m_current_image];
    m_images[m_current_image] = NULL;
    m_images.erase(m_images.begin() + m_current_image);

    // Update image indices
    // This should be done for all image indices we may be storing
    if (m_last_image == m_current_image) {
        if (!m_images.empty() && m_last_image > 0)
            m_last_image = 0;
        else
            m_last_image = -1;
    }
    if (m_last_image > m_current_image)
        m_last_image--;

    m_current_image = m_current_image < (int)m_images.size() ? m_current_image
                                                             : 0;
    displayCurrentImage();
}



void
ImageViewer::print()
{
#if 0
    Q_ASSERT(imageLabel->pixmap());
    QPrintDialog dialog(&printer, this);
    if (dialog.exec()) {
        QPainter painter(&printer);
        QRect rect = painter.viewport();
        QSize size = imageLabel->pixmap()->size();
        size.scale(rect.size(), Qt::KeepAspectRatio);
        painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
        painter.setWindow(imageLabel->pixmap()->rect());
        painter.drawPixmap(0, 0, *imageLabel->pixmap());
    }
#endif
}



void
ImageViewer::zoomIn()
{
    IvImage* img = cur();
    if (!img)
        return;
    if (zoom() >= 64)
        return;
    float oldzoom = zoom();
    float newzoom = ceil2f(oldzoom);

    float xc, yc;  // Center view position
    glwin->get_center(xc, yc);
    int xm, ym;  // Mouse position
    glwin->get_focus_image_pixel(xm, ym);
    float xoffset      = xc - xm;
    float yoffset      = yc - ym;
    float maxzoomratio = std::max(oldzoom / newzoom, newzoom / oldzoom);
    int nsteps         = (int)OIIO::clamp(20 * (maxzoomratio - 1), 2.0f, 10.0f);
    for (int i = 1; i <= nsteps; ++i) {
        float a         = (float)i / (float)nsteps;  // Interpolation amount
        float z         = OIIO::lerp(oldzoom, newzoom, a);
        float zoomratio = z / oldzoom;
        view(xm + xoffset / zoomratio, ym + yoffset / zoomratio, z, false);
        if (i != nsteps) {
            QApplication::processEvents();
            Sysutil::usleep(1000000 / 4 / nsteps);
        }
    }

    fitImageToWindowAct->setChecked(false);
}



void
ImageViewer::zoomOut()
{
    IvImage* img = cur();
    if (!img)
        return;
    if (zoom() <= 1.0f / 64)
        return;
    float oldzoom = zoom();
    float newzoom = floor2f(oldzoom);

    float xcpel, ycpel;  // Center view position
    glwin->get_center(xcpel, ycpel);
    int xmpel, ympel;  // Mouse position
    glwin->get_focus_image_pixel(xmpel, ympel);
    float xoffset      = xcpel - xmpel;
    float yoffset      = ycpel - ympel;
    float maxzoomratio = std::max(oldzoom / newzoom, newzoom / oldzoom);
    int nsteps         = (int)OIIO::clamp(20 * (maxzoomratio - 1), 2.0f, 10.0f);
    for (int i = 1; i <= nsteps; ++i) {
        float a         = (float)i / (float)nsteps;  // Interpolation amount
        float z         = OIIO::lerp(oldzoom, newzoom, a);
        float zoomratio = z / oldzoom;
        view(xmpel + xoffset / zoomratio, ympel + yoffset / zoomratio, z,
             false);
        if (i != nsteps) {
            QApplication::processEvents();
            Sysutil::usleep(1000000 / 4 / nsteps);
        }
    }

    fitImageToWindowAct->setChecked(false);
}


void
ImageViewer::normalSize()
{
    IvImage* img = cur();
    if (!img)
        return;
    fitImageToWindowAct->setChecked(false);
    float xcenter = img->oriented_full_x() + 0.5 * img->oriented_full_width();
    float ycenter = img->oriented_full_y() + 0.5 * img->oriented_full_height();
    view(xcenter, ycenter, 1.0, true);
    fitWindowToImage(false);
}



float
ImageViewer::zoom_needed_to_fit(int w, int h)
{
    IvImage* img = cur();
    if (!img)
        return 1;
    float zw = (float)w / img->oriented_width();
    float zh = (float)h / img->oriented_height();
    return std::min(zw, zh);
}



void
ImageViewer::fitImageToWindow()
{
    IvImage* img = cur();
    if (!img)
        return;
    fitImageToWindowAct->setChecked(true);
    zoom(zoom_needed_to_fit(glwin->width(), glwin->height()));
}



void
ImageViewer::fitWindowToImage(bool zoomok, bool minsize)
{
    IvImage* img = cur();
    // Don't resize when there's no image or the image hasn't been opened yet
    // (or we failed to open it).
    if (!img || !img->image_valid())
        return;
        // FIXME -- figure out a way to make it exactly right, even for the
        // main window border, etc.
#ifdef __APPLE__
    int extraw = 0;  //12; // width() - minimumWidth();
    int extrah = statusBar()->height()
                 + 0;  //40; // height() - minimumHeight();
#else
    int extraw = 4;  //12; // width() - minimumWidth();
    int extrah = statusBar()->height()
                 + 4;  //40; // height() - minimumHeight();
#endif
    //    std::cerr << "extra wh = " << extraw << ' ' << extrah << '\n';

    float z = zoom();
    int w   = (int)(img->oriented_full_width() * z) + extraw;
    int h   = (int)(img->oriented_full_height() * z) + extrah;
    if (minsize) {
        if (w < m_default_width) {
            w = m_default_width;
        }
        if (h < m_default_height) {
            h = m_default_height;
        }
    }

    if (!m_fullscreen) {
        QDesktopWidget* desktop = QApplication::desktop();
        QRect availgeom         = desktop->availableGeometry(this);
        int availwidth          = availgeom.width() - extraw - 20;
        int availheight = availgeom.height() - extrah - menuBar()->height()
                          - 20;
#if 0
        QRect screengeom = desktop->screenGeometry (this);
        std::cerr << "available desktop geom " << availgeom.x() << ' ' << availgeom.y() << ' ' << availgeom.width() << "x" << availgeom.height() << "\n";
        std::cerr << "screen desktop geom " << screengeom.x() << ' ' << screengeom.y() << ' ' << screengeom.width() << "x" << screengeom.height() << "\n";
#endif
        if (w > availwidth || h > availheight) {
            w = std::min(w, availwidth);
            h = std::min(h, availheight);
            if (zoomok) {
                z = zoom_needed_to_fit(w, h);
                w = (int)(img->oriented_full_width() * z) + extraw;
                h = (int)(img->oriented_full_height() * z) + extrah;
                // std::cerr << "must rezoom to " << z << " to fit\n";
            }
            // std::cerr << "New window geom " << w << "x" << h << "\n";
            int posx = x(), posy = y();
            if (posx + w > availwidth || posy + h > availheight) {
                if (posx + w > availwidth)
                    posx = std::max(0, availwidth - w) + availgeom.x();
                if (posy + h > availheight)
                    posy = std::max(0, availheight - h) + availgeom.y();
                // std::cerr << "New position " << posx << ' ' << posy << "\n";
                move(QPoint(posx, posy));
            }
        }
    }

    float midx = img->oriented_full_x() + 0.5 * img->oriented_full_width();
    float midy = img->oriented_full_y() + 0.5 * img->oriented_full_height();
    view(midx, midy, z, false, false);
    resize(w, h);  // Resize will trigger a repaint.

#if 0
    QRect g = geometry();
    std::cerr << "geom " << g.x() << ' ' << g.y() << ' ' << g.width() << "x" << g.height() << "\n";
    g = frameGeometry();
    std::cerr << "frame geom " << g.x() << ' ' << g.y() << ' ' << g.width() << "x" << g.height() << "\n";
    g = glwin->geometry();
    std::cerr << "ogl geom " << g.x() << ' ' << g.y() << ' ' << g.width() << "x" << g.height() << "\n";
    std::cerr << "Status bar height = " << statusBar()->height() << "\n";
#endif

#if 0
    bool fit = fitWindowToImageAct->isChecked();
    if (!fit) {
        normalSize();
    }
#endif
    updateActions();
}



void
ImageViewer::fullScreenToggle()
{
    if (m_fullscreen) {
        menuBar()->show();
        statusBar()->show();
        showNormal();
        m_fullscreen = false;
        slideTimer->stop();
        disconnect(slideTimer, 0, 0, 0);
    } else {
        menuBar()->hide();
        statusBar()->hide();
        showFullScreen();
        m_fullscreen = true;
        fitImageToWindow();
    }
}



void
ImageViewer::about()
{
    QMessageBox::about(
        this, tr("About iv"),
        tr("<p><b>iv</b> is the image viewer for OpenImageIO.</p>"
           "<p>(c) Copyright Contributors to the OpenImageIO project.</p>"
           "<p>See <a href='http://openimageio.org'>http://openimageio.org</a> for details.</p>"));
}


void
ImageViewer::updateActions()
{
    //    zoomInAct->setEnabled(!fitImageToWindowAct->isChecked());
    //    zoomOutAct->setEnabled(!fitImageToWindowAct->isChecked());
    //    normalSizeAct->setEnabled(!fitImageToWindowAct->isChecked());
}



static inline void
calc_subimage_from_zoom(const IvImage* img, int& subimage, float& zoom,
                        float& xcenter, float& ycenter)
{
    int rel_subimage = std::trunc(std::log2(1.0f / zoom));
    subimage         = clamp<int>(img->subimage() + rel_subimage, 0,
                          img->nsubimages() - 1);
    if (!(img->subimage() == 0 && zoom > 1)
        && !(img->subimage() == img->nsubimages() - 1 && zoom < 1)) {
        float pow_zoom = powf(2.0f, (float)rel_subimage);
        zoom *= pow_zoom;
        xcenter /= pow_zoom;
        ycenter /= pow_zoom;
    }
}



void
ImageViewer::view(float xcenter, float ycenter, float newzoom, bool smooth,
                  bool redraw)
{
    IvImage* img = cur();
    if (!img)
        return;

    float oldzoom = m_zoom;
    float oldxcenter, oldycenter;
    glwin->get_center(oldxcenter, oldycenter);
    float zoomratio = std::max(oldzoom / newzoom, newzoom / oldzoom);
    int nsteps      = (int)OIIO::clamp(20 * (zoomratio - 1), 2.0f, 10.0f);
    if (!smooth || !redraw)
        nsteps = 1;
    for (int i = 1; i <= nsteps; ++i) {
        float a  = (float)i / (float)nsteps;  // Interpolation amount
        float xc = OIIO::lerp(oldxcenter, xcenter, a);
        float yc = OIIO::lerp(oldycenter, ycenter, a);
        m_zoom   = OIIO::lerp(oldzoom, newzoom, a);

        glwin->view(xc, yc, m_zoom, redraw);  // Triggers redraw automatically
        if (i != nsteps) {
            QApplication::processEvents();
            Sysutil::usleep(1000000 / 4 / nsteps);
        }
    }

    if (img->auto_subimage()) {
        int subimage = 0;
        calc_subimage_from_zoom(img, subimage, m_zoom, xcenter, ycenter);
        if (subimage != img->subimage()) {
            //std::cerr << "Changing to subimage " << subimage;
            //std::cerr << " With zoom: " << m_zoom << '\n';
            loadCurrentImage(subimage);
            glwin->update();
            glwin->view(xcenter, ycenter, m_zoom, redraw);
        }
    }

    //    zoomInAct->setEnabled (zoom() < 64.0);
    //    zoomOutAct->setEnabled (zoom() > 1.0/64);

    updateStatusBar();
}



void
ImageViewer::zoom(float newzoom, bool smooth)
{
    float xcenter, ycenter;
    glwin->get_center(xcenter, ycenter);
    view(xcenter, ycenter, newzoom, smooth);
}



void
ImageViewer::showInfoWindow()
{
    if (!infoWindow) {
        infoWindow = new IvInfoWindow(*this, true);
        infoWindow->setPalette(m_palette);
    }
    infoWindow->update(cur());
    if (infoWindow->isHidden())
        infoWindow->show();
    else
        infoWindow->hide();
}



void
ImageViewer::showPixelviewWindow()
{
    ((QOpenGLWidget*)(glwin))->update();
}



void
ImageViewer::editPreferences()
{
    if (!preferenceWindow) {
        preferenceWindow = new IvPreferenceWindow(*this);
        preferenceWindow->setPalette(m_palette);
    }
    preferenceWindow->show();
}
