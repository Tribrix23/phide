<?php
$result = [
    'functions' => [],
    'classes' => [],
    'constants' => [],
    'class_constants' => [],
    'class_methods' => [],
];

function formatParam(ReflectionParameter $p): string {
    $s = '';
    if ($p->hasType()) {
        $s .= (string)$p->getType() . ' ';
    }
    if ($p->isVariadic()) {
        $s .= '...';
    }
    if ($p->isPassedByReference()) {
        $s .= '&';
    }
    $s .= '$' . $p->getName();
    if ($p->isDefaultValueAvailable()) {
        try {
            if ($p->isDefaultValueConstant()) {
                $s .= ' = ' . $p->getDefaultValueConstantName();
            } else {
                $val = $p->getDefaultValue();
                if ($val === null) $s .= ' = null';
                elseif ($val === true) $s .= ' = true';
                elseif ($val === false) $s .= ' = false';
                elseif (is_array($val)) $s .= ' = []';
                elseif (is_string($val)) $s .= ' = ' . json_encode($val);
                elseif (is_numeric($val)) $s .= ' = ' . $val;
            }
        } catch (Throwable $e) {}
    } elseif ($p->isOptional() && !$p->isVariadic()) {
        $s .= ' = null';
    }
    return $s;
}

function formatCallable(ReflectionFunctionAbstract $rf): string {
    $params = [];
    foreach ($rf->getParameters() as $p) {
        $params[] = formatParam($p);
    }
    $ret = 'mixed';
    if ($rf->hasReturnType()) {
        $ret = (string)$rf->getReturnType();
    } elseif ($rf->hasTentativeReturnType()) {
        $ret = (string)$rf->getTentativeReturnType();
    }
    return '(' . implode(', ', $params) . '): ' . $ret;
}

foreach (get_defined_functions()['internal'] as $fn) {
    try {
        $rf = new ReflectionFunction($fn);
        $ext = $rf->getExtensionName() ?: 'PHP';
        $result['functions'][$rf->getName()] = [
            'sig' => formatCallable($rf),
            'ext' => $ext
        ];
    } catch (Throwable $e) {}
}

$allClasses = array_merge(get_declared_classes(), get_declared_interfaces(), get_declared_traits());
foreach ($allClasses as $cls) {
    try {
        $rc = new ReflectionClass($cls);
        $name = $rc->getName();
        $ext = $rc->getExtensionName() ?: 'Core';
        $kind = $rc->isInterface() ? 'Interface' : ($rc->isTrait() ? 'Trait' : ($rc->isEnum() ? 'Enum' : 'Class'));
        $result['classes'][$name] = [
            'kind' => $kind,
            'ext' => $ext
        ];
        foreach ($rc->getReflectionConstants(ReflectionClassConstant::IS_PUBLIC) as $const) {
            if ($const->getDeclaringClass()->getName() !== $name && $name !== 'PDO' && $name !== 'DateTime') continue;
            $val = $const->getValue();
            $vStr = is_scalar($val) ? var_export($val, true) : gettype($val);
            $result['class_constants'][$name . '::' . $const->getName()] = [
                'class' => $name,
                'name' => $const->getName(),
                'val' => $vStr,
                'type' => gettype($val)
            ];
        }
        foreach ($rc->getMethods(ReflectionMethod::IS_PUBLIC) as $m) {
            $mName = $m->getName();
            $sig = formatCallable($m);
            $isStatic = $m->isStatic();
            $result['class_methods'][$name . ($isStatic ? '::' : '->') . $mName] = [
                'class' => $name,
                'name' => $mName,
                'sig' => $sig,
                'static' => $isStatic
            ];
        }
    } catch (Throwable $e) {}
}

foreach (get_defined_constants(true) as $cat => $consts) {
    if ($cat === 'user') continue;
    foreach ($consts as $k => $v) {
        if (str_starts_with($k, '__')) continue;
        $vStr = is_scalar($v) ? (is_bool($v) ? ($v ? 'true' : 'false') : (string)$v) : gettype($v);
        if (strlen($vStr) > 40) $vStr = substr($vStr, 0, 40) . '...';
        $result['constants'][$k] = [
            'cat' => $cat,
            'type' => gettype($v),
            'val' => $vStr
        ];
    }
}

file_put_contents(__DIR__ . '/php_reflection.json', json_encode($result, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
echo "Exported: "
    . count($result['functions']) . " functions, "
    . count($result['classes']) . " classes, "
    . count($result['class_constants']) . " class constants, "
    . count($result['class_methods']) . " class methods, "
    . count($result['constants']) . " global constants.\n";
