import {WebglPlot, ColorRGBA, WebglLine} from "./webgl-plot/dist/webglplot.esm.js";

// TODO: Input buffer + downsampling for extended periods
// TODO: Interactivity (pause, zoom, select, time on axis)

export class Plot {
    constructor(canvasID, numX, cfg) {
        this.canvasID = canvasID;
        this.canvas = document.getElementById(`canvas_${canvasID}`);
        const devicePixelRatio = window.devicePixelRatio || 1;
        this.canvas.width = this.canvas.clientWidth * devicePixelRatio;
        this.canvas.height = this.canvas.clientHeight * devicePixelRatio;
        console.log(`Plot on canvas ${canvasID}: ${this.canvas.width} x ${this.canvas.height}`);

        this.numX = numX;
        this.cfg = cfg[this.canvasID];

        this.numLines = this.cfg.numLines;

        this.plot = new WebglPlot(this.canvas);
        this.plot.removeAllLines();
        this.lines = [];

        let nLine = 0;
        for (const [partitionID, partition] of Object.entries(this.cfg.partitions)) {
            for (let i = 0; i < partition.numLines; i++) {
                const rgb = getColor(document.getElementById(`${partitionID}_${i}`).style.backgroundColor)
                const color = new ColorRGBA(rgb[0], rgb[1], rgb[2], 1);

                const line = new WebglLine(color, this.numX);
                line.lineSpaceX(-1, 2 / this.numX); // fill x with numX -1...1 array
                line.offsetY = 2*(this.numLines-nLine-1)/(this.numLines)-1;
                line.scaleY = partition.scaleFactors ? partition.scaleFactors[i] : 1;
                this.plot.addLine(line);
                this.lines.push(line);
                nLine++;
            }
        }

        // fill with zeros at start
        let data = new Array(numX).fill(0);
        this.lines.forEach((line) => {
            line.shiftAdd(data);
        });
        this.plot.gScaleX = 1;
    }

    update_data(packets) {
        if (packets == null) return;
        let nLine = 0;
        for (const [partitionID, partition] of Object.entries(this.cfg.partitions)) {
            for (let i=0; i < partition.numLines; i++) {
                const data = [];
                packets.forEach((packet) => {
                    data.push(packet[partition.dataID][i]);
                });
                this.lines[nLine].shiftAdd(data);
                nLine++;
            }
        }
    }
}

function getColor(rgb) {
    let sep = rgb.indexOf(",") > -1 ? "," : " ";
    rgb = rgb.substr(4).split(")")[0].split(sep);
    let r = parseInt(rgb[0]),
        g = parseInt(rgb[1]),
        b = parseInt(rgb[2]);
    return [r / 255, g / 255, b / 255];
}