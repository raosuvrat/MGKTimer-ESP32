let gateway = `ws://${window.location.hostname}/ws`;
let websocket, ping_setinterval, ping_settimeout;
let history = localStorage.getItem("history") || {};
let start_time = 0, dispinterval;
let banner_timeout = 2000, ping_timeout = 5000, ws_timeout = 2000;

function format_us(t_start, t) {
    let dur = t - t_start, tthous = dur / 100, cents = tthous / 100, secs = cents / 100,
        mins = secs / 60, hrs = mins / 60, days = hrs / 24;
    return (
        days > 1 ? String(Math.floor(days)) + ":" : "" +
            hrs > 1 ? String(Math.floor(hrs % 24)).padStart(2, '0') + ":" : "" +
            String(Math.floor(mins % 60)).padStart(2, '0') + ":" +
            String(Math.floor(secs % 60)).padStart(2, '0') + ":" +
        String(Math.floor(cents % 100)).padStart(2, '0')
    );
}

function init_websocket() {
    websocket = new WebSocket(gateway);
    websocket.onopen = (e) => {
        $(".alert-success").html("Websocket connection opened!").removeClass("d-none");
        $(".alert-danger").addClass("d-none");
        $("#controls").removeAttr("disabled");
        setTimeout(() => {
            $(".alert-success").addClass("d-none");
        }, banner_timeout);
        ping_setinterval = setInterval(() => {
            websocket.send('__ping__');
            ping_settimeout = setTimeout(() => {
                $(".alert-danger").html("Websocket ping timeout").removeClass("d-none");
                websocket.close();
            }, ping_timeout);
        }, ping_timeout);

    };
    websocket.onclose = (e) => {
        $(".alert-danger").html("Websocket connection closed").removeClass("d-none");
        $("#controls").attr("disabled", "disabled");
        setTimeout(init_websocket, ws_timeout);
        clearInterval(ping_setinterval);
    };
    websocket.onerror = (e) => {
        $(".alert-danger").html("Websocket error").removeClass("d-none");
        websocket.close();
    };
    websocket.onmessage = (e) => {
        // console.log(e.data);
        if (e.data == "__pong__") {
            clearTimeout(ping_settimeout);
            console.log("pong");
            return;
        }
        let doc = JSON.parse(e.data);
        $("#data").html(`<pre>${JSON.stringify(doc, false, 1).replace(/[\{\}\",]/g, '')}</pre>`);
        $("#mode-dropdown ul li a.active").removeClass("active");
        $(`#mode-dropdown ul li a[data-value='${doc["mode"]}']`).addClass("active");
        $("#crossings-dropdown ul li a.active").removeClass("active");
        $(`#crossings-dropdown ul li a[data-value='${doc["crossings"]}']`).addClass("active");
        $("#intensity-slider").val(doc["intensity"]);
        $("#threshold-slider").val(doc["adc_threshold"]);
        $("#intensity-value").html(doc["intensity"]);
        $("#threshold-value").html(doc["adc_threshold"]);
        $("#lockout-slider").val(doc["beam_cross_lockout_ms"]);
        $("#lockout-value").html(doc["beam_cross_lockout_ms"]);

        // if (doc["start"] != 0 && doc["finish"] == 0 && dispinterval === undefined) {
        if (doc["msg"] == "running" && dispinterval === undefined) {
            start_time = (Date.now() * 1000);
            dispinterval = setInterval(() => {
                $("#display").html(format_us(start_time, Date.now() * 1000));
            }, 100);
        } else if (doc["msg"] == "finish") {
            if (dispinterval !== undefined) {
                clearInterval(dispinterval);
                dispinterval = undefined;
            }
            $("#display").html(format_us(doc["start"], doc["finish"]));
            $("#display-micros").html(doc["finish"] - doc["start"]);
        } else if (doc["msg"] == "ready") {
            if (dispinterval !== undefined) {
                clearInterval(dispinterval);
                dispinterval = undefined;
            }
            // $("#display").html("00:00:00");
        }
    };
}
$(document).ready(() => {
    init_websocket();
    $("#mode-dropdown ul li a").on('click', (e) => {
        let selected_mode = e.currentTarget.getAttribute("data-value");
        $("#mode-dropdown ul li a.active").removeClass("active");
        $(`#mode-dropdown ul li a[data-value='${selected_mode}']`).addClass("active");
        websocket.send(JSON.stringify({ "mode": selected_mode }));
    });
    $("#crossings-dropdown ul li a").on('click', (e) => {
        let selected_crossings = e.currentTarget.getAttribute("data-value");
        $("#crossings-dropdown ul li a.active").removeClass("active");
        $(`#crossings-dropdown ul li a[data-value='${selected_crossings}']`).addClass("active");
        websocket.send(JSON.stringify({ "crossings": selected_crossings }));
    });
    $('#intensity-slider').on('change', (e) => {
        let selected_intensity = e.target.value;
        $("#intensity-value").html(selected_intensity);
        websocket.send(JSON.stringify({ "intensity": selected_intensity }));
    });
    $('#threshold-slider').on('change', (e) => {
        let selected_threshold = e.target.value;
        $("#threshold-value").html(selected_threshold);
        websocket.send(JSON.stringify({ "adc_threshold": selected_threshold }));
    });
    $('#lockout-slider').on('change', (e) => {
        let selected_lockout = e.target.value;
        $("#lockout-value").html(selected_lockout);
        websocket.send(JSON.stringify({ "beam_cross_lockout_ms": selected_lockout }));
    });
    $('#lockout-enable').on('change', (e) => {
        $("#lockout-slider").attr("disabled", !e.target.checked);
        $("#crossings-dropdown button").toggleClass("disabled", !e.target.checked);
        websocket.send(JSON.stringify({ "beam_cross_lockout_ms": e.target.checked ? e.target.value : 0 }));
    });
});