import { downloadFile, type Status } from './api';
import { useEffect, useRef } from 'react';
import { shortName } from './blockLabels';

type Props = { status: Status; command: (cmd: string, body?: Record<string, unknown>) => Promise<Record<string, unknown> | undefined> };
export function ActionControls({ status, command }: Props) {
  const actions = status.pendingActions ?? [];
  const panel = useRef<HTMLDivElement>(null);
  useEffect(() => { panel.current?.scrollIntoView({ block: 'nearest' }); }, [actions[0]?.id]);
  if (!actions.length) return null;
  const action = actions[0];
  return <div ref={panel} className="inspectorSection actionControls" aria-label="待处理外部动作">
    <label className="miniLabel">外部动作 · {actions.length} 项待处理</label>
    <strong>弹出 {shortName(action.item)} × {action.count}</strong>
    <p className="subtleText">{action.tick} gt · 来源 ({action.source.join(', ')})<br/>
      位置 {action.position.map(v => v.toFixed(4)).join(', ')}<br/>
      速度 {action.velocity.map(v => v.toFixed(4)).join(', ')} 格/gt</p>
    <p className="subtleText">掉落物的后续运动由外部环境处理。可先在器件面板输入反馈，再确认；无需反馈时可直接确认。</p>
    <button className="wideButton accentOutline" onClick={() => command('resolveAction', { id: action.id, revision: status.revision })}>确认已处理</button>
    <button className="wideButton" onClick={async () => {
      const data = await command('actionLog');
      if (data) downloadFile('environmentActions.json', JSON.stringify({ actions: data.actions, dropped: data.dropped }, null, 2));
    }}>导出外部动作记录</button>
    {status.actionsDropped > 0 && <p className="subtleText">已截断 {status.actionsDropped} 条已确认记录。</p>}
  </div>;
}
