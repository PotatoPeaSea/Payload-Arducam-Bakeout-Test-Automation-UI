import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import CameraUI 1.0

Window {
    id: wizard
    title: mode + " Test Wizard"
    width: 700; height: 540
    color: "#E8E8E8"
    flags: Qt.Window | Qt.WindowCloseButtonHint

    // ── Public API ────────────────────────────────────────────────
    property string mode: "Pre-Bakeout"   // or "Post-Bakeout"

    // ── Internal state ────────────────────────────────────────────
    property int  stepIndex:   -1         // index into baseSteps; -1 = start screen
    property bool inLoop:      false      // true once we enter the capture/rotate loop
    property int  currentAngle: 0         // 0, 15, 30 … 345
    property bool loopCapture: true       // true = auto capture, false = instruction rotate
    property bool complete:    false
    property bool waitAuto:    false      // true = waiting for an auto step to finish
    property string autoErrorMsg: ""
    property real  exposureResult: 0
    property bool waitingForStream: false
    property bool streamOpened: false
    property var  capturedImagesList: []

    // ── Base steps (before the 24-angle loop) ─────────────────────
    readonly property var baseSteps: {
        var s = []
        if (mode === "Pre-Bakeout") {
            s.push({ type:"auto",        action:"initialize",
                     title:"Starting Camera Preview",
                     body:"Initializing JPEG mode, setting low resolution (320×240), and starting the live preview…" })
            s.push({ type:"instruction", title:"Physical Setup  1 / 7",
                     body:"Find a physical area at least 230 cm long with a flat, level surface. Verify it is level using a spirit level." })
            s.push({ type:"instruction", title:"Physical Setup  2 / 7",
                     body:"Ensure there are no variable lighting conditions so that lighting stays constant. This area must also be available for the Post-Bakeout test. Ideally a room with no windows." })
            s.push({ type:"instruction", title:"Physical Setup  3 / 7",
                     body:"Position the ArduCam so that the lens faces horizontally — parallel to the ground, not angled up toward the sky or down toward the floor." })
            s.push({ type:"instruction", title:"Physical Setup  4 / 7",
                     body:"Place the target exactly 191.5 cm from the camera, measured from the end of the lens face." })
            s.push({ type:"instruction", title:"Physical Setup  5 / 7",
                     body:"Check the ArduCam Host live preview. The entire target must be in frame and must fill the entire frame. Reposition if needed." })
            s.push({ type:"instruction", title:"Physical Setup  6 / 7",
                     body:"Once correctly positioned, secure the ArduCam and all wires with tape. Do not move it again." })
            s.push({ type:"instruction", title:"Physical Setup  7 / 7",
                     body:"If there are variable lighting conditions (e.g., windows), record them now so they can be replicated during the Post-Bakeout test." })
        } else {
            s.push({ type:"instruction", title:"Post-Bakeout Setup",
                     body:"Replicate the exact physical setup used during the Pre-Bakeout test:\n\n" +
                          "• Same room and position\n" +
                          "• Camera lens parallel to ground\n" +
                          "• Target 191.5 cm from the lens face\n" +
                          "• Lighting conditions matching Pre-Bakeout records\n\n" +
                          "Click Next once the setup matches." })
            s.push({ type:"auto", action:"initialize",
                     title:"Starting Camera Preview",
                     body:"Initializing JPEG mode, setting low resolution (320×240), and starting the live preview…" })
            s.push({ type:"instruction", title:"Verify Setup",
                     body:"Check the live preview in the ArduCam Host window. Confirm the target fills the frame exactly as it did during the Pre-Bakeout test. Reposition if needed, then click Next." })
        }
        // Both modes share: stop-stream → calibrate → record
        s.push({ type:"auto", action:"calibrate",
                 title:"Calibrating Exposure",
                 body:"Stopping stream and running automatic exposure calibration.\nAdjusting until average brightness = 160 ± 5…" })
        s.push({ type:"instruction", action:"recordExposure",
                 title:"Record Exposure Value",
                 body:"" })   // body filled dynamically
        s.push({ type:"auto", action:"setMaxRes",
                 title:"Setting Resolution",
                 body:"Setting camera resolution to maximum (2592x1944)…" })
        return s
    }

    // ── Step helpers ──────────────────────────────────────────────
    property var currentStepObj: _computeStep()

    function _computeStep() {
        if (stepIndex < 0) return { type:"start" }
        if (complete)      return { type:"done"  }
        if (!inLoop && stepIndex < baseSteps.length) {
            var st = baseSteps[stepIndex]
            if (st.action === "recordExposure") {
                return { type:"instruction", title:st.title,
                         body:"Calibration complete!\n\nExposure value: " +
                              exposureResult.toFixed(0) + " µs\n\nRecord this value and click Next." }
            }
            return st
        }
        if (loopCapture)
            return { type:"auto", action:"captureImages",
                     title:"Capturing Images  –  " + currentAngle + "°",
                     body:"Taking 5 images at " + currentAngle + "°…" }
        else
            return { type:"instruction", title:"Rotate Apparatus",
                     body:"Rotate the test apparatus by 15°.\n\nCurrent angle: " + currentAngle +
                          "°\nNext angle:    " + (currentAngle + 15) + "°\n\nClick Next once rotated." }
    }

    function _refreshStep() { currentStepObj = _computeStep() }

    property int totalSteps: baseSteps.length + 24 + 23
    property int currentStepNum: {
        if (stepIndex < 0) return 0
        if (complete) return totalSteps
        if (!inLoop)  return stepIndex + 1
        var iter = Math.floor(currentAngle / 15)
        return baseSteps.length + iter * 2 + (loopCapture ? 1 : 2)
    }

    // ── Navigation logic ──────────────────────────────────────────
    function _trigger() {
        var st = currentStepObj
        if (st.type !== "auto") { waitAuto = false; return }
        waitAuto = true
        if (st.action === "initialize") {
            waitingForStream = true
            streamOpened = false
            ArduCam.jpegInit(); ArduCam.setResolution(0); ArduCam.startStreaming()
            // advance once stream ACK is parsed
        } else if (st.action === "calibrate") {
            BakeoutCtrl.stopStreamingIfActive()
            calibrateDelay.start()
        } else if (st.action === "setMaxRes") {
            ArduCam.setResolution(6)
        } else if (st.action === "captureImages") {
            capturedImagesList = []
            BakeoutCtrl.createFolder(mode)
            BakeoutCtrl.captureImages(mode, mode, currentAngle, 5)
        }
    }

    function goNext() {
        if (waitAuto) return
        if (stepIndex < 0)   { stepIndex = 0; _refreshStep(); _trigger(); return }
        if (complete)        { wizard.close(); return }

        if (!inLoop) {
            stepIndex++
            if (stepIndex >= baseSteps.length) { inLoop = true; loopCapture = true }
            _refreshStep(); _trigger()
        } else if (!loopCapture) {
            currentAngle += 15
            if (currentAngle >= 360) { complete = true; _refreshStep() }
            else { loopCapture = true; _refreshStep(); _trigger() }
        }
    }

    function goBack() {
        if (waitAuto || stepIndex <= 0) return
        if (complete) { complete = false; _refreshStep(); return }
        if (inLoop && !loopCapture) {
            if (currentAngle === 0) { inLoop = false; stepIndex = baseSteps.length - 1 }
            else { currentAngle -= 15; loopCapture = true }
        } else if (inLoop && loopCapture) {
            if (currentAngle === 0) { inLoop = false; stepIndex = baseSteps.length - 1 }
            else { currentAngle -= 15; loopCapture = false }
        } else {
            stepIndex--
        }
        _refreshStep()
    }

    Timer { id: calibrateDelay; interval: 300; repeat: false
            onTriggered: BakeoutCtrl.startCalibration() }

    // BakeoutCtrl signals
    Connections {
        target: BakeoutCtrl
        function onCalibrationDone(expUs) {
            exposureResult = expUs
            waitAuto = false
            stepIndex++
            wizard._refreshStep()
        }
        function onCalibrationFailed(reason) {
            waitAuto = false
            autoErrorMsg = "Calibration failed: " + reason
        }
        function onImageCaptured(path, idx) {
            var arr = capturedImagesList
            arr.push("file:///" + path)
            capturedImagesList = arr
        }
        function onImagesSaved(count) {
            waitAuto = false
            loopCapture = false
            wizard._refreshStep()
        }
    }

    // ArduCam connections for busy and stream ACK checking
    Connections {
        target: ArduCam
        function onFrameCounterChanged() {
            if (waitingForStream && ArduCam.streaming) {
                waitingForStream = false
                streamOpened = true
                waitAuto = false // Allow user to click Next
                wizard._refreshStep()
            }
        }
        function onBusyChanged() {
            if (!ArduCam.busy && waitAuto && currentStepObj.action === "setMaxRes") {
                waitAuto = false
                Qt.callLater(function() { wizard._refreshStep() })
            }
        }
    }

    // ── UI ────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true; height: 56; color: "#2B2B2B"
            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                Column {
                    Label { text: mode + " Test Wizard"; font.pixelSize: 15; font.bold: true; color: "white" }
                    Label {
                        text: complete ? "Complete ✓"
                              : stepIndex < 0 ? "Ready to begin"
                              : "Step " + currentStepNum + " of " + totalSteps
                        font.pixelSize: 11; color: "#AAAAAA"
                    }
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    width: badge.implicitWidth + 18; height: 24; radius: 12
                    color: mode === "Pre-Bakeout" ? "#1E6B3C" : "#1A3F6B"
                    Label { id: badge; anchors.centerIn: parent
                            text: mode; color: "white"; font.pixelSize: 11; font.bold: true }
                }
            }
        }

        ProgressBar { Layout.fillWidth: true; from: 0; to: totalSteps; value: currentStepNum; height: 5 }

        // Content
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

            // ── Start screen
            Column {
                anchors.centerIn: parent; spacing: 16
                visible: stepIndex < 0 && !complete
                Label { anchors.horizontalCenter: parent.horizontalCenter
                        text: mode === "Pre-Bakeout" ? "🎯" : "✅"; font.pixelSize: 48 }
                Label { anchors.horizontalCenter: parent.horizontalCenter
                        text: "Ready to Start"; font.pixelSize: 20; font.bold: true }
                Label { width: 460; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                        text: mode === "Pre-Bakeout"
                              ? "This wizard will guide you through the Pre-Bakeout image acquisition procedure. Make sure the camera is connected before starting."
                              : "This wizard will guide you through the Post-Bakeout image acquisition procedure. Make sure the camera is in the same location as during the Pre-Bakeout test." }
            }

            // ── Instruction step
            Column {
                anchors.centerIn: parent; spacing: 20; width: parent.width * 0.82
                visible: !complete && stepIndex >= 0 && currentStepObj.type === "instruction"
                Label { text: currentStepObj.title ?? ""; font.pixelSize: 18; font.bold: true
                        wrapMode: Text.WordWrap; width: parent.width }
                Label { text: currentStepObj.body ?? ""; font.pixelSize: 13
                        wrapMode: Text.WordWrap; width: parent.width; lineHeight: 1.5 }
                
                Column {
                    visible: currentStepObj.title === "Rotate Apparatus"
                    spacing: 12; width: parent.width
                    Label { text: "Images captured for " + currentAngle + "°:" ; font.bold: true }
                    Row {
                        spacing: 8
                        Repeater {
                            model: capturedImagesList
                            Image {
                                source: modelData
                                cache: false
                                width: 90; height: 67
                                fillMode: Image.PreserveAspectCrop
                            }
                        }
                    }
                    Button {
                        text: "Retake Images for " + currentAngle + "°"
                        onClicked: {
                            capturedImagesList = []
                            loopCapture = true
                            wizard._refreshStep()
                            wizard._trigger()
                        }
                    }
                }
            }

            // ── Auto step
            Column {
                anchors.centerIn: parent; spacing: 20; width: parent.width * 0.82
                visible: !complete && stepIndex >= 0 && currentStepObj.type === "auto"
                Label { text: currentStepObj.title ?? ""; font.pixelSize: 18; font.bold: true
                        wrapMode: Text.WordWrap; width: parent.width }
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: waitAuto }
                
                Label { 
                    visible: currentStepObj.action === "initialize"
                    text: waitingForStream ? "Initializing JPEG mode and waiting for stream to open..." 
                          : streamOpened   ? "Stream successfully opened. Check the live preview, then click Next." 
                          : "Initializing..."
                    font.pixelSize: 14; wrapMode: Text.WordWrap; width: parent.width
                    color: streamOpened ? "#1E6B3C" : "black"
                    horizontalAlignment: Text.AlignHCenter
                }

                Label { 
                    visible: currentStepObj.action !== "initialize"
                    text: BakeoutCtrl.status; font.family: "Consolas"; font.pixelSize: 12
                    wrapMode: Text.WordWrap; width: parent.width; opacity: 0.8 
                }

                Row {
                    spacing: 5
                    visible: currentStepObj.action === "captureImages" && capturedImagesList.length > 0
                    Repeater {
                        model: capturedImagesList
                        Image {
                            source: modelData
                            cache: false
                            width: 90; height: 67
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                }
                Label { visible: autoErrorMsg !== ""; text: "⚠ " + autoErrorMsg
                        color: "#CC3300"; wrapMode: Text.WordWrap; width: parent.width }
            }

            // ── Done screen
            Column {
                anchors.centerIn: parent; spacing: 16; visible: complete
                Label { anchors.horizontalCenter: parent.horizontalCenter
                        text: "🏁"; font.pixelSize: 48 }
                Label { anchors.horizontalCenter: parent.horizontalCenter
                        text: mode + " Complete!"; font.pixelSize: 20; font.bold: true }
                Label { width: 460; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                        text: "All 24 angles captured (5 images each = 120 images total).\nImages saved to the \"" + mode + "\" folder." }
            }
        }

        // Navigation bar
        Rectangle {
            Layout.fillWidth: true; height: 56; color: "#D4D4D4"
            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                Button {
                    text: "← Back"
                    enabled: !waitAuto && stepIndex > 0 && !complete
                    onClicked: wizard.goBack()
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: waitAuto
                    text: "Processing…"; font.pixelSize: 12; opacity: 0.6
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: stepIndex < 0 ? "Start →"
                          : complete    ? "Close"
                          : (currentStepObj.type === "auto" && waitAuto) ? "Please wait…"
                          : "Next →"
                    enabled: !waitAuto
                    highlighted: true
                    onClicked: wizard.goNext()
                }
            }
        }
    }
}
