// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include "citra/camera/camera_util.h"
#include "citra/camera/qt_camera_base.h"

namespace Camera {

QtCameraInterface::QtCameraInterface(const Service::CAM::Flip& flip) {
    using namespace Service::CAM;
    flip_horizontal = basic_flip_horizontal = (flip == Flip::Horizontal) || (flip == Flip::Reverse);
    flip_vertical = basic_flip_vertical = (flip == Flip::Vertical) || (flip == Flip::Reverse);
}

void QtCameraInterface::SetFormat(Service::CAM::OutputFormat output_format) {
    output_rgb = output_format == Service::CAM::OutputFormat::RGB565;
}

void QtCameraInterface::SetResolution(const Service::CAM::Resolution& resolution) {
    width = resolution.width;
    height = resolution.height;
}

void QtCameraInterface::SetFlip(Service::CAM::Flip flip) {
    using namespace Service::CAM;
    flip_horizontal = basic_flip_horizontal ^ (flip == Flip::Horizontal || flip == Flip::Reverse);
    flip_vertical = basic_flip_vertical ^ (flip == Flip::Vertical || flip == Flip::Reverse);
}

void QtCameraInterface::SetEffect(Service::CAM::Effect effect) {
    if (effect != Service::CAM::Effect::None)
        LOG_ERROR(Service_CAM, "Unimplemented effect {}", static_cast<int>(effect));
}

std::vector<u16> QtCameraInterface::ReceiveFrame() {
    return CameraUtil::ProcessImage(QtReceiveFrame(), width, height, output_rgb, flip_horizontal,
                                    flip_vertical);
}

std::unique_ptr<CameraInterface> QtCameraFactory::CreatePreview(const std::string& config,
                                                                int width, int height,
                                                                const Service::CAM::Flip& flip) {
    std::unique_ptr<CameraInterface> camera{Create(config, flip)};
    if (camera->IsPreviewAvailable())
        return camera;
    QMessageBox::critical(nullptr, "Error",
                          (config.empty()
                               ? "Couldn't load the camera"
                               : QString("Couldn't load %1").arg(QString::fromStdString(config))));
    return nullptr;
}

} // namespace Camera
