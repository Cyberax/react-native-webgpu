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
      width: number,
      height: number,
    ) => RNRawCanvasContext;
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

export type RNRawCanvasContext = Exclude<
  RNCanvasContext,
  "getCurrentTexture"
> & {
  getCurrentTexture: (width: number, height: number) => GPUTexture;
};

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
  const viewRef = useRef(null);
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

        const view = viewRef.current;
        const size = getViewSize(view);
        const pixelRatio = PixelRatio.get();

        let canvasContext = RNWebGPU.MakeWebGPUCanvasContext(
          contextId,
          Math.ceil(size.width * pixelRatio),
          Math.ceil(size.height * pixelRatio),
        );

        let canvasObj: CanvasElement = {
          width: size.width,
          height: size.height,
          clientWidth: size.width * pixelRatio,
          clientHeight: size.height * pixelRatio,
          surface: 0n,
        };

        // Add the size adjuster to the getCurrentTexture
        return {
          __brand: canvasContext.__brand,
          canvas: canvasObj as unknown as HTMLCanvasElement, // Yeah, not great
          configure: canvasContext.configure,
          unconfigure: canvasContext.unconfigure,
          getConfiguration: canvasContext.getConfiguration,
          getCurrentTexture: function () {
            const sz = getViewSize(view);
            if (sz.width <= 0) {
              // See MDN: https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement
              // for the default size explanation.
              sz.width = 300;
            }
            if (sz.height <= 0) {
              sz.height = 150;
            }
            // The canvas size is calculated from the view size. From the viewpoint of clients,
            // it can't be controlled by changing the canvas element anymore
            canvasObj.width = sz.width;
            canvasObj.height = sz.height;
            canvasObj.clientWidth = sz.width * pixelRatio;
            canvasObj.clientHeight = sz.height * pixelRatio;

            // The native side works with on the raw device pixel level.
            // The CSS pixels are left behind in the JavaScript world.
            return canvasContext.getCurrentTexture(
              Math.ceil(sz.width * pixelRatio),
              Math.ceil(sz.height * pixelRatio),
            );
          },
          present: canvasContext.present,
        } satisfies RNCanvasContext;
      },
    };
  });

  return (
    <View collapsable={false} ref={viewRef} {...props}>
      <WebGPUNativeView
        style={{ flex: 1 }}
        contextId={contextId}
        transparent={!!transparent}
      />
    </View>
  );
};
