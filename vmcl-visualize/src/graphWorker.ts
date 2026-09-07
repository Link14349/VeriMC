import ELK from "elkjs/lib/elk-api";
import elkWorkerUrl from "elkjs/lib/elk-worker.min.js?url";
import { readVmcl } from "./vmclReader";
import { layoutGraph } from "./layout";
self.onmessage = async (event: MessageEvent<{ text: string }>) => {
  try {
    const graph = readVmcl(event.data.text);
    self.postMessage({ phase: "layout" });
    const elk = new ELK({ workerFactory: () => new Worker(elkWorkerUrl) });
    let layout;
    try {
      layout = await layoutGraph(graph, elk);
    } finally {
      elk.terminateWorker();
    }
    self.postMessage({ graph, layout });
  } catch (error) {
    self.postMessage({
      error: error instanceof Error ? error.message : String(error),
    });
  }
};
