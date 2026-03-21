import sys
import numpy as np
import pandas as pd
import plotly.graph_objects as go

file_name = sys.argv[1]
df = pd.read_csv('xzx_data.csv')

custom_data = np.stack((df['pageable'], df['device_user'], df['device_allocator'], df['pinned_user'], df['pinned_allocator'], df['total'], df['function'], df['file'], df['line']), axis=-1)

fig = go.Figure()
#fig.add_trace(go.Scatter(x = df['id'], y = df['device_allocator'], mode='lines', name='Allocator', line = dict(color='#ef553b')))
#fig.add_trace(go.Scatter(x = df['id'], y = df['device_user'], mode='lines', name='User', line = dict(color='#636efa', dash='dot')))
fig.add_trace(go.Scatter(x = df['id'], y = df['pageable'], mode='lines', name='Pageable memory'))
fig.add_trace(go.Scatter(x = df['id'], y = df['device_user'], mode='lines', name='Device memory (user)'))
fig.add_trace(go.Scatter(x = df['id'], y = df['device_allocator'], mode='lines', name='Device memory (allocator)'))
fig.add_trace(go.Scatter(x = df['id'], y = df['pinned_user'], mode='lines', name='Pinned memory (user)'))
fig.add_trace(go.Scatter(x = df['id'], y = df['pinned_allocator'], mode='lines', name='Pinned memory (allocator)'))
fig.add_trace(go.Scatter(x = df['id'], y = df['total'], mode='lines', name='Total memory'))


fig.data[0].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Pageable memory: %{customdata[0]} bytes")

fig.data[1].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Device memory (user): %{customdata[1]} bytes")

fig.data[2].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Device memory (allocator): %{customdata[2]} bytes")

fig.data[3].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Pinned memory (user): %{customdata[3]} bytes")

fig.data[4].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Pinned memory (allocator): %{customdata[4]} bytes")

fig.data[5].update(customdata = custom_data,
                    hovertemplate =
                    "Function: %{customdata[6]} <br>" +
                    "File: %{customdata[7]} <br>" +
                    "Line: %{customdata[8]} <br>" +
                    "Total memory: %{customdata[5]} bytes")

fig.update_layout(
    title='Memory characterization of ' + file_name,
    xaxis_title="Memory operation number",
    yaxis_title="Amount of memory (bytes)",
    #legend_title="Memory",
    plot_bgcolor='rgb(230, 230, 230)',
    showlegend=True,
    hoverlabel=dict(bgcolor="white"),
    xaxis=dict(tickmode='auto',tick0=1,dtick=1)
)

fig.write_html(file_name + '.html', auto_open=False)

fig.update_layout(showlegend=False, title='', margin=dict(l=20, r=20, t=20, b=20))
fig.update_layout(font=dict(size=14))
fig.update_layout(autosize=False, width=440, height=300)
fig.write_image(file_name + '_2.pdf')

fig.update_layout(yaxis_title='')
fig.write_image(file_name + '_1.pdf')


