(function (global) {
    var errorCount = 0;
    var logDiv;

    function shouldThrow(expression) {
        try {
            eval(expression);
        } catch (e) {
            log("PASS: '" + expression + "' did throw");
            return;
        }
        reportError("FAIL: '" + expression + "' should have thrown");
    }

    function shouldNotThrow(expression) {
        try {
            eval(expression);
        } catch (e) {
            reportError("FAIL: '" + expression + "' should not have thrown");
            return;
        }
        log("PASS: '" + expression + "' did not throw");
    }

    function getErrorCount() {
        return errorCount;
    }

    function reportError(message) {
        log(message);
        errorCount++;
    }

    function log(message) {
        if (!logDiv) {
            logDiv = document.createElement("div");
            document.body.appendChild(logDiv);
        }
        logDiv.appendChild(document.createTextNode(message));
        logDiv.appendChild(document.createElement("br"));
    }

    global.shouldThrow = shouldThrow;
    global.shouldNotThrow = shouldNotThrow;
    global.getErrorCount = getErrorCount;
    global.reportError = reportError;
    global.log = log;

})(self);
