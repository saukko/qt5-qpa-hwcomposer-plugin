/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Thomas Perl <thomas.perl@jolla.com>
**
** This file is part of the hwcomposer plugin.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "hwcomposer_backend_v11.h"

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_win(NULL)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , oldretire(-1)
    , oldrelease(-1)
    , oldrelease2(-1)
{
    HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 0));
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Destroy the window if it hasn't yet been destroyed
    if (hwc_win != NULL) {
        delete hwc_win;
    }

    // Close the hwcomposer handle
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwc_device));

    if (hwc_mList != NULL) {
        free(hwc_mList);
    }

    if (hwc_list != NULL) {
        free(hwc_list);
    }
}

EGLNativeDisplayType
HwComposerBackend_v11::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v11::createWindow(int width, int height)
{
    // We expect that we haven't created a window already, if we had, we
    // would leak stuff, and we want to avoid that for obvious reasons.
    HWC_PLUGIN_EXPECT_NULL(hwc_win);
    HWC_PLUGIN_EXPECT_NULL(hwc_list);
    HWC_PLUGIN_EXPECT_NULL(hwc_mList);

    hwc_win = new HWComposerNativeWindow(width, height, HAL_PIXEL_FORMAT_RGBA_8888);

    size_t neededsize = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
    hwc_list = (hwc_display_contents_1_t *) malloc(neededsize);
    hwc_mList = (hwc_display_contents_1_t **) malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
    const hwc_rect_t r = { 0, 0, width, height };

    for (int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
         hwc_mList[i] = hwc_list;
    }

    hwc_layer_1_t *layer = NULL;

    layer = &hwc_list->hwLayers[0];
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = r;
    layer->displayFrame = r;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    layer = &hwc_list->hwLayers[1];
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER_TARGET;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = r;
    layer->displayFrame = r;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    hwc_list->retireFenceFd = -1;
    hwc_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_list->numHwLayers = 2;

    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);

    // FIXME: Implement (delete hwc_win + set it to NULL?)
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    // TODO: Wait for vsync?

    HWC_PLUGIN_ASSERT_NOT_NULL(hwc_win);

    HWComposerNativeWindowBuffer *front;
    hwc_win->lockFrontBuffer(&front);

    hwc_mList[0]->hwLayers[1].handle = front->handle;
    hwc_mList[0]->hwLayers[0].handle = NULL;
    hwc_mList[0]->hwLayers[0].flags = HWC_SKIP_LAYER;

    oldretire = hwc_mList[0]->retireFenceFd;
    oldrelease = hwc_mList[0]->hwLayers[0].releaseFenceFd;
    oldrelease2 = hwc_mList[0]->hwLayers[1].releaseFenceFd;

    HWC_PLUGIN_ASSERT_ZERO(hwc_device->prepare(hwc_device, HWC_NUM_DISPLAY_TYPES, hwc_mList));
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->set(hwc_device, HWC_NUM_DISPLAY_TYPES, hwc_mList));

    hwc_win->unlockFrontBuffer(front);

    if (oldrelease != -1) {
        sync_wait(oldrelease, -1);
        close(oldrelease);
    }

    if (oldrelease2 != -1) {
        sync_wait(oldrelease2, -1);
        close(oldrelease2);
    }

    if (oldretire != -1) {
        sync_wait(oldretire, -1);
        close(oldretire);
    }
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    // TODO
}

float
HwComposerBackend_v11::refreshRate()
{
    // TODO: Implement new hwc 1.1 querying of vsync period per-display
    //
    // from hwcomposer_defs.h:
    // "This query is not used for HWC_DEVICE_API_VERSION_1_1 and later.
    //  Instead, the per-display attribute HWC_DISPLAY_VSYNC_PERIOD is used."
    return 60.0;
}

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
