# Lepton Data Product Decoder and Viewer
### How to Use
1. activate your fprime python virtual environment
2. downlink lepton data following the typical mission operator workflow
    1. ArtemisRpiTeensyDeployment.missionManager.ENTER_BASE_MODE
    2. ArtemisRpiTeensyDeployment.sohManager.EMIT_SOH_SNAPSHOT
    3. ArtemisRpiTeensyDeployment.commsManager.PING_LINK_RSSI
    4. ArtemisRpiTeensyDeployment.scienceManager.CONFIGURE_CAPTURE_DURATION (i do 5 secs for testing, but iirc lepton payload only takes 1 pic regardless)
    5. ArtemisRpiTeensyDeployment.payloadAdapterLepton.ENABLE (this opens the camera stream)
    6. ArtemisRpiTeensyDeployment.missionManager.SCHEDULE_COLLECTION (i do 5 secs delay for testing. you should see a DataProducts.dpCat.NotLoaded event in the events log, you can ignore the message, but it implies that the DpWriter successfully wrote the data product file in ./DpCat/ on the pi)
    7. (optional) ArtemisRpiTeensyDeployment.storageService.REPORT_LATEST_DATASET (run this to verify storage service can see the new file)
    8. ArtemisRpiTeensyDeployment.payloadAdapterLepton.DISABLE
    9. make sure payload_receiver.py is running. i use:
        ```bash
        python payload_receiver.py \
        --port /dev/cu.usbmodem115553305 \
        --output-dir ~/Desktop/hsfl/fprime-artemis-cubesat/TEST-DATA-DOWNLINK \
        --ext .fdp \
        --debug
        ```
    9. ArtemisRpiTeensyDeployment.commsManager.REQUEST_SCIENCE_DOWNLINK
3. in payload receiver, you'll see a message like:
    ```bash
    saved: Dp_20260707_120740.fdp product=6421489 transfer=1 bytes=38480 packets=1100 [ok]
    ```
4. now you can run dp_lepton_viewer.py to decode the .fdp file and save the data as a .json, .csv, and .png
    ```bash
    python dp_lepton_viewer.py /Users/samanthamallari/Desktop/hsfl/fprime-artemis-cubesat/TEST-DATA-DOWNLINK/Dp_20260707_120740.fdp

    usage: dp_lepton_viewer.py [-h]
                           [--dictionary DICTIONARY]
                           [--outdir OUTDIR]
                           [--no-show]
                           bin_file

    Decode + view a Lepton thermal data product (.fdp)

    positional arguments:
    bin_file              path to the .fdp data product
                            file

    options:
    -h, --help            show this help message and
                            exit
    --dictionary DICTIONARY
                            path to the deployment JSON
                            dictionary (auto-detected if
                            omitted)
    --outdir OUTDIR       output directory for
                            .json/.csv/.png (default:
                            cwd)
    --no-show             save the PNG but don't open a
                            window
    ```