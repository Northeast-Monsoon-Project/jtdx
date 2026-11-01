#!/usr/bin/env python3
"""Optional conformance smoke test using the official MCP Python SDK."""
import asyncio
from contextlib import AsyncExitStack
import os
import secrets
import socket
import subprocess
import sys
try:
    import httpx2 as httpx
except ImportError:
    import httpx
from mcp import ClientSession, StdioServerParameters
from mcp.client.streamable_http import streamable_http_client
from mcp.client.stdio import stdio_client
from pathlib import Path

async def main():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    token=secrets.token_hex(32)
    env=dict(os.environ,JTDX_MCP_TOKEN=token,JTDX_MCP_URL=f'http://127.0.0.1:{port}/mcp')
    proc=subprocess.Popen([sys.argv[1],str(port)],env=env,stdout=subprocess.PIPE)
    assert proc.stdout.readline().strip()==b'ready'
    try:
        async with httpx.AsyncClient(headers={'Authorization':'Bearer '+token},trust_env=False) as http:
            async with streamable_http_client(env['JTDX_MCP_URL'],http_client=http) as streams:
                read,write=streams[:2]
                async with ClientSession(read,write) as session:
                    init=await session.initialize()
                    assert init.model_dump(by_alias=True)['serverInfo']['name']=='JTDX-F'
                    assert len((await session.list_tools()).tools)==22
                    assert len((await session.list_resources()).resources)==5
                    assert not (await session.call_tool('get_status')).model_dump(by_alias=True)['isError']
                    assert (await session.call_tool('start_tx')).model_dump(by_alias=True)['isError']
                    assert not (await session.call_tool('stop_tx')).model_dump(by_alias=True)['isError']
                    await session.read_resource('jtdx://status')
                    await session.get_prompt('operate_ft8')
        bridge=Path(__file__).resolve().parents[1]/'tools/jtdx_mcp_bridge.py'
        async with stdio_client(StdioServerParameters(command=sys.executable,args=[str(bridge)],env=env)) as (read,write):
            async with ClientSession(read,write) as session:
                await session.initialize()
                assert len((await session.list_tools()).tools)==22
                assert not (await session.call_tool('get_status')).model_dump(by_alias=True)['isError']
        print('Official MCP SDK: HTTP and stdio passed')
    finally:
        proc.terminate(); proc.wait(timeout=5); proc.stdout.close()

asyncio.run(main())
