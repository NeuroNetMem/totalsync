export class CameraView {
    constructor(host_uri, port=8111) {
        this.videoUri = `http://169.254.14.129:${port}/stream.mjpg`;
        this.view = document.getElementById('videoStream');
        this.view.src = this.videoUri;
        this.t_last_frame = null;

        this.view.onerror = (e) => {
            console.error(e);
            this.view.src = '';
        };

        this.view.onload = (e) => {
            // each time a new frame is loaded?
            if (this.t_last_frame != null) {
                let elapsed = Date.now() - this.t_last_frame;

                // if (elapsed > 1.0) {
                //     console.log()
                // }
            }

            this.t_last_frame = Date.now();
        };

        const reload_button = document.getElementById("videoStreamButton");
        reload_button.onclick = this.reload
    }

    reload = () => {
        this.view.src = this.videoUri;
    }
}