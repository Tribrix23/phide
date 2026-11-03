<?php
if (
    isset($_SERVER['REQUEST_METHOD']) &&
    $_SERVER['REQUEST_METHOD'] === 'GET' &&
    !isset($_GET['__phide_mtime']) &&
    empty($_SERVER['HTTP_X_REQUESTED_WITH'])
) {
    $isHtml = true;
    foreach (headers_list() as $h) {
        if (stripos($h, 'Content-Type:') === 0 && stripos($h, 'text/html') === false) {
            $isHtml = false;
            break;
        }
        if (stripos($h, 'Location:') === 0) {
            $isHtml = false;
            break;
        }
    }
    if ($isHtml) {
        echo "\n<script>(function(){var last=null;setInterval(function(){fetch(window.location.pathname+'?__phide_mtime='+Date.now(),{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){var v=parseInt(t,10);if(v>0){if(last!==null&&v!==last){window.location.reload();}last=v;}}).catch(function(){});},500);})();</script>";
    }
}
