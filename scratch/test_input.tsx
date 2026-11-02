"use client";

import React, { useState } from "react";
import { Check, Copy, Terminal } from "lucide-react";

interface CopyCommandProps {
  command: string;
}

export function CopyCommand({ command }: CopyCommandProps) {
  const [copied, setCopied] = useState(false);

  const handleCopy = () => {
    navigator.clipboard.writeText(command);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="flex items-center gap-3 bg-zinc-950/5 border border-zinc-200/60 rounded-lg px-4 py-2 font-mono text-sm shadow-sm">
      <Terminal className="w-4 h-4 text-zinc-400" />
      <span className="text-zinc-700">{command}</span>
      <button
        onClick={handleCopy}
        className="ml-auto flex items-center justify-center w-8 h-8 rounded-md hover:bg-zinc-200/50 transition-colors text-zinc-500 hover:text-zinc-900"
        title="Copy to clipboard"
        aria-label="Copy to clipboard"
      >
        {copied ? <Check className="w-4 h-4 text-emerald-500" /> : <Copy className="w-4 h-4" />}
      </button>
    </div>
  );
}
