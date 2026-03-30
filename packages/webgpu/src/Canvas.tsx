import React, { useImperativeHandle, useRef, useState } from "react";
import type { ViewProps } from "react-native";
import { PixelRatio, View } from "react-native";

import WebGPUNativeView from "./WebGPUViewNativeComponent";

let CONTEXT_COUNTER = 1;
function generateContextId() {
  return CONTEXT_COUNTER++;
}

declare global {
  var RNWebGPU: {
    gpu: GPU;
    fabric: boolean;
    getNativeSurface: (contextId: number) => NativeCanvas;
    MakeWebGPUCanvasContext: (
      contextId: number,
      pixelRatio: number,
      measurer: () => { width: number; height: number },
    ) => RNCanvasContext;
    DecodeToUTF8: (buffer: NodeJS.ArrayBufferView | ArrayBuffer) => string;
    createImageBitmap: typeof createImageBitmap;
  };
}

type SurfacePointer = bigint;

export interface NativeCanvas {
  surface: SurfacePointer;
  width: number;
  height: number;
  clientWidth: number;
  clientHeight: number;
}

export type RNCanvasContext = GPUCanvasContext & {
  present: () => void;
};

export interface CanvasRef {
  getContextId: () => number;
  getContext(contextName: "webgpu"): RNCanvasContext | null;
  getNativeSurface: () => NativeCanvas;
}

interface CanvasProps extends ViewProps {
  transparent?: boolean;
  ref?: React.Ref<CanvasRef>;
}

function getViewSize(view: View): { width: number; height: number } {
  // getBoundingClientRect became stable in RN 0.83
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const viewAny = view as any;
  const size =
    "getBoundingClientRect" in viewAny
      ? viewAny.getBoundingClientRect()
      : viewAny.unstable_getBoundingClientRect();
  return size;
}

export type CanvasElement = {
  width: number;
  height: number;
  clientWidth: number;
  clientHeight: number;
  surface: bigint;
};

export const Canvas = ({ transparent, ref, ...props }: CanvasProps) => {
  const viewRef = useRef<View>(null);
  const contextRef = useRef<RNCanvasContext | null>(null);
  const [contextId, _] = useState(() => generateContextId());
  useImperativeHandle(ref, () => {
    return {
      getContextId: () => contextId,
      getNativeSurface: () => {
        return RNWebGPU.getNativeSurface(contextId);
      },
      getContext(contextName: "webgpu"): RNCanvasContext | null {
        if (contextName !== "webgpu") {
          throw new Error(`[WebGPU] Unsupported context: ${contextName}`);
        }
        if (!viewRef.current) {
          throw new Error("[WebGPU] Cannot get context before mount");
        }
        if (contextRef.current) {
          return contextRef.current;
        }

        const view = viewRef.current;
        const pixelRatio = PixelRatio.get();
        const weakView = new WeakRef(view);

        const measurer = function () {
          // We need to use a weak ref to break the loop between the GPU context and the view
          const cur = weakView.deref();
          if (!cur) {
            return { width: 0, height: 0 };
          }
          return getViewSize(cur);
        };

        contextRef.current = RNWebGPU.MakeWebGPUCanvasContext(
          contextId,
          pixelRatio,
          measurer,
        );
        return contextRef.current;
      },
    };
  });

  const withNativeId = { ...props, nativeID: `webgpu-container-${contextId}` };
  return (
    <View collapsable={false} ref={viewRef} {...withNativeId}>
      <WebGPUNativeView
        style={{ flex: 1 }}
        contextId={contextId}
        transparent={!!transparent}
      />
    </View>
  );
};
