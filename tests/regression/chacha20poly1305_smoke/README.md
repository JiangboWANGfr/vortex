# ChaCha20-Poly1305 AEAD —— 算法的数学描述

> 本文用 LaTeX 数学公式描述算法流程，请在支持数学渲染的查看器里阅读
> （VS Code 内置 Markdown 预览、GitHub 均可）。

ChaCha20-Poly1305 AEAD（RFC 8439）由两个原语组成：

- **ChaCha20**：ARX 流密码（在环 $\mathbb{Z}/2^{32}$ 上做加-旋转-异或），负责机密性。
- **Poly1305**：素域 $\mathbb{F}_p$（$p = 2^{130}-5$）上的多项式求值 MAC，负责完整性。

记号约定：

| 记号 | 含义 |
| --- | --- |
| $a \boxplus b$ | 模 $2^{32}$ 加法 $(a+b)\bmod 2^{32}$ |
| $a \oplus b$ | 按位异或 |
| $a \lll n$ | 32-bit 循环左移 $n$ 位 |
| $a \mathbin{\&} m$ | 按位与 |
| $\lfloor x \rfloor$ | 向下取整 |
| $x \bmod m$ | 非负余数 |
| $\,\|\,$ | 字节串拼接 |

---

## 一、ChaCha20（流密码）

### 1.1 状态

状态是 16 个 32-bit 字组成的向量 $S \in (\mathbb{Z}/2^{32})^{16}$（逻辑上排成 $4\times4$ 矩阵）：

$$
S = (\,\underbrace{c_0,c_1,c_2,c_3}_{\text{常量}},\;\; \underbrace{k_0,k_1,\ldots,k_7}_{\text{256-bit 密钥}},\;\; \underbrace{t}_{\text{计数器}},\;\; \underbrace{n_0,n_1,n_2}_{\text{96-bit nonce}}\,)
$$

- 常量 $(c_0,c_1,c_2,c_3) = (\texttt{0x61707865},\texttt{0x3320646e},\texttt{0x79622d32},\texttt{0x6b206574})$
  （即 ASCII `"expand 32-byte k"` 的小端编码）。
- 密钥字 $k_i = \sum_{b=0}^{3} K[4i+b]\cdot 2^{8b}$，nonce 字 $n_i$ 同理小端读取。

### 1.2 四分之一轮 QR

$\mathrm{QR}(a,b,c,d)$ 把 4 个字按下式顺序更新（旋转量固定 $16/12/8/7$）：

$$
\begin{aligned}
a &\leftarrow a \boxplus b, & d &\leftarrow (d \oplus a) \lll 16,\\
c &\leftarrow c \boxplus d, & b &\leftarrow (b \oplus c) \lll 12,\\
a &\leftarrow a \boxplus b, & d &\leftarrow (d \oplus a) \lll 8,\\
c &\leftarrow c \boxplus d, & b &\leftarrow (b \oplus c) \lll 7.
\end{aligned}
$$

### 1.3 轮函数

一轮由 4 个 $\mathrm{QR}$ 组成；**列轮** $\mathrm{ROUND}_{\mathrm{col}}$ 作用在矩阵的列上，
**对角轮** $\mathrm{ROUND}_{\mathrm{diag}}$ 作用在对角线上，下标四元组为：

| $\mathrm{QR}$ | 列轮下标 | 对角轮下标 |
| --- | --- | --- |
| 1 | $(0,4,8,12)$ | $(0,5,10,15)$ |
| 2 | $(1,5,9,13)$ | $(1,6,11,12)$ |
| 3 | $(2,6,10,14)$ | $(2,7,8,13)$ |
| 4 | $(3,7,11,15)$ | $(3,4,9,14)$ |

一个**双轮** $\mathrm{DR} = \mathrm{ROUND}_{\mathrm{diag}} \circ \mathrm{ROUND}_{\mathrm{col}}$。
（每种轮的 4 个四元组恰好划分 $\{0,\ldots,15\}$，故轮内 4 个 $\mathrm{QR}$ 互不相干、可并行。）

### 1.4 块函数

20 轮 $=$ 10 个双轮，末尾与初始状态逐字相加（feedforward）：

$$
X = \mathrm{DR}^{10}(S), \qquad \mathrm{ChaCha20\_block}(S) = X \boxplus S
$$

输出 64 字节密钥流块 $Z$ 由 $X \boxplus S$ 的 16 个字小端序列化：
$Z[4i+b] = \big\lfloor (X_i \boxplus S_i)/2^{8b} \big\rfloor \bmod 2^8$。

> feedforward 保证不可逆：$\mathrm{DR}^{10}$ 是双射（可逆置换），单独输出会泄露 $S$；
> 加回 $S$ 后 $X \boxplus S$ 不可逆。

### 1.5 加密

明文按 64 字节切块 $P = P_0 \mathbin{\|} P_1 \mathbin{\|} \cdots$，第 $j$ 块用计数器 $t = t_0 + j$：

$$
Z_j = \mathrm{ChaCha20\_block}(S \mid t = t_0+j), \qquad C_j = P_j \oplus Z_j.
$$

各块相互独立（仅计数器不同）。解密同式 $P_j = C_j \oplus Z_j$。

---

## 二、Poly1305（多项式 MAC）

### 2.1 密钥与钳制

256-bit 密钥拆为两个 128-bit 半：低半 $r$（乘子）、高半 $s$（最后叠加的掩码 pad）。
$r$ 须**钳制**（clamp）：

$$
r \leftarrow r \mathbin{\&} \texttt{0x0ffffffc0ffffffc0ffffffc0fffffff}
$$

（即清掉 $r$ 第 $3/7/11/15$ 字节的高 4 位、第 $4/8/12$ 字节的低 2 位。）钳制保证下文 limb
乘积有界，并去除弱密钥。

### 2.2 消息块编码

把消息按 16 字节切块。第 $i$ 块（$L$ 字节，$1 \le L \le 16$）读成小端整数，并在最高有效字节
之后追加一个 1 比特：

$$
c_i = \begin{cases}
\displaystyle\sum_{b=0}^{15} m_i[b]\,2^{8b} + 2^{128}, & L = 16 \quad(\text{满块}),\\[2.2ex]
\displaystyle\sum_{b=0}^{L-1} m_i[b]\,2^{8b} + 2^{8L}, & 1 \le L < 16 \quad(\text{残块}).
\end{cases}
$$

即每块被解释成一个 $(8L+1)$ 比特整数 $c_i$。

### 2.3 Horner 累加（核心）

设共 $q$ 块。在素域 $p = 2^{130}-5$ 上递推：

$$
a_0 = 0, \qquad a_i = (a_{i-1} + c_i)\,r \bmod p, \quad i = 1,\ldots,q.
$$

展开即一个以 $r$ 为变量的多项式求值：

$$
a_q = \sum_{i=1}^{q} c_i\, r^{\,q+1-i} \bmod p
    = c_1 r^{q} + c_2 r^{q-1} + \cdots + c_q r \pmod p.
$$

### 2.4 标签

$$
\mathrm{tag} = (a_q + s) \bmod 2^{128}.
$$

---

## 三、Poly1305 的 limb 级算术（2.3 那步乘法怎么算）

每步 $a \leftarrow (a+c)\,r \bmod p$ 不用 130-bit 大数，而用 **radix-$2^{26}$、5 肢**定点表示。
取基 $b = 2^{26}$：

$$
a = \sum_{i=0}^{4} a_i\,b^{i}, \qquad r = \sum_{j=0}^{4} r_j\,b^{j}, \qquad 0 \le a_i, r_j < 2^{26}.
$$

（$130 = 5\times 26$，故 5 肢即可表示 $[0,2^{130})$。）

### 3.1 多项式乘积

$$
a\cdot r = \sum_{i=0}^{4}\sum_{j=0}^{4} a_i r_j\, b^{\,i+j}, \qquad i+j \in \{0,1,\ldots,8\}.
$$

本应产生 9 个肢位置 $b^0,\ldots,b^8$。

### 3.2 模 $p$ 折叠（$2^{130} \equiv 5$）

由 $b^{5} = 2^{130} \equiv 5 \pmod p$，对任意 $t \ge 0$ 有 $b^{\,5+t} \equiv 5\,b^{\,t}$。于是把
$i+j \ge 5$ 的高位项"乘 5、下移 5 肢"折回低位，合并同次幂得 5 个（尚未进位的）肢值：

$$
d_k = \underbrace{\sum_{i+j=k} a_i r_j}_{\text{直接项}} \;+\; \underbrace{5\!\!\sum_{i+j=k+5}\!\! a_i r_j}_{\text{回绕项}}, \qquad k = 0,1,2,3,4.
$$

逐肢展开（代码把 $5r_j$ 预算为数组 `r5[j]`）：

$$
\begin{aligned}
d_0 &= a_0 r_0 + 5a_1 r_4 + 5a_2 r_3 + 5a_3 r_2 + 5a_4 r_1,\\
d_1 &= a_0 r_1 + a_1 r_0 + 5a_2 r_4 + 5a_3 r_3 + 5a_4 r_2,\\
d_2 &= a_0 r_2 + a_1 r_1 + a_2 r_0 + 5a_3 r_4 + 5a_4 r_3,\\
d_3 &= a_0 r_3 + a_1 r_2 + a_2 r_1 + a_3 r_0 + 5a_4 r_4,\\
d_4 &= a_0 r_4 + a_1 r_3 + a_2 r_2 + a_3 r_1 + a_4 r_0.
\end{aligned}
$$

> ⚠ 命名：这里的 $5r_j$（代码数组 `r5`）只是化简用的预算系数，**与 §2.1 那个最后叠加的
> pad $s$ 无关**。（poly1305-donna 把 $5r$ 数组记作 `s`，是常见歧义来源；本仓库代码已改名 `r5`。）

### 3.3 $d_0,\ldots,d_4$ 是什么？为什么用 64-bit？

**含义**：$d_0,\ldots,d_4$ 是乘积 $a\cdot r \bmod p$ 的 5 个肢，但**尚未进位**的"宽"中间值
（故记作 $d$）。每个 $d_k$ 把"落在第 $k$ 肢的直接项"与"折回到第 $k$ 肢的回绕项（已 $\times 5$）"加在一起。

以 $d_0$ 为例，第 0 肢的全部来源：

| 项 | $(i,j)$ | $i+j$ | 类型 |
| --- | --- | --- | --- |
| $a_0 r_0$ | $(0,0)$ | $0$ | 直接项 |
| $5\,a_1 r_4$ | $(1,4)$ | $5$ | 回绕项 |
| $5\,a_2 r_3$ | $(2,3)$ | $5$ | 回绕项 |
| $5\,a_3 r_2$ | $(3,2)$ | $5$ | 回绕项 |
| $5\,a_4 r_1$ | $(4,1)$ | $5$ | 回绕项 |

$d_1$ 是第 1 肢（$i+j=1$ 的直接项 $+$ $i+j=6$ 的回绕项），依此类推；$d_4$ 只有直接项
（$i+j=4$），因为 $i+j=9$ 不存在（$i,j$ 最大为 $4$）。

**位宽（为什么必须 64-bit）**：加块（延迟进位）后 $a_i \lesssim 2^{27}$，钳制后 $r_j < 2^{26}$，
故 $5r_j < 2^{29}$。单项

$$
a_i \cdot 5r_j < 2^{27}\cdot 2^{29} = 2^{56},
$$

每个 $d_k$ 至多 5 项之和：

$$
d_k < 5\cdot 2^{56} < 2^{59}.
$$

这超过 32-bit（$2^{32}$）但稳进 64-bit（$2^{64}$）。**这正是选 26-bit 肢的原因**：让"乘积之和"
恰好落在 64-bit 内，无需更宽的中间类型。

### 3.4 进位归一

把 $d_k$ 规整回 $[0,2^{26})$ 的标准肢，进位逐肢上传，顶肢进位按 $2^{130}\equiv 5$ 回绕：

$$
\begin{aligned}
&\text{对 } k = 0,1,2,3: && a_k = d_k \bmod 2^{26}, && d_{k+1} \mathrel{+}= \lfloor d_k/2^{26}\rfloor,\\
& && a_4 = d_4 \bmod 2^{26}, && a_0 \mathrel{+}= 5\,\lfloor d_4/2^{26}\rfloor.
\end{aligned}
$$

（实现里再补传一次 $a_0 \to a_1$，使全部肢 $< 2^{26}$。）

### 3.5 终约简（freeze）

Horner 全部块吃完后 $a \in [0,2^{130}) = [0,p+5)$，故 $a$ 至多比 $p$ 大 4，**最多减一次 $p$**
即得唯一规范代表：

$$
a \leftarrow \begin{cases} a - p, & a \ge p,\\ a, & a < p. \end{cases}
$$

随后取 $a \bmod 2^{128}$ 并按 §2.4 叠加 $s$ 得 16 字节 tag。

---

## 四、ChaCha20-Poly1305 AEAD（组合）

输入：密钥 $K$（256-bit）、nonce $N$（96-bit）、关联数据 $A$、明文 $P$。

$$
\begin{aligned}
\text{(1) 一次性 MAC 密钥:}\quad & r \mathbin{\|} s = \mathrm{ChaCha20\_block}(K, t{=}0, N)\ \text{的前 32 字节},\\
\text{(2) 加密:}\quad & C = \mathrm{ChaCha20\text{-}Enc}(K, t_0{=}1, N, P),\\
\text{(3) 认证数据:}\quad & \mu = A \mathbin{\|} 0^{\alpha} \mathbin{\|} C \mathbin{\|} 0^{\gamma} \mathbin{\|} \mathrm{len}_{64}(|A|) \mathbin{\|} \mathrm{len}_{64}(|C|),\\
\text{(4) 标签:}\quad & T = \mathrm{Poly1305}(r\mathbin{\|}s,\ \mu).
\end{aligned}
$$

其中：

- 填充长度 $\alpha = (-|A|) \bmod 16$、$\gamma = (-|C|) \bmod 16$，使 $A$、$C$ 各自补零到 16 字节边界；
- $\mathrm{len}_{64}(x)$ 为 $x$ 的 64-bit 小端编码（字节数）；
- $t=0$ 的块用于派生 MAC 密钥，故明文加密从 $t_0=1$ 起，保证每条消息（$N$ 不同）的 Poly1305
  密钥互不相同。

由 $\alpha,\gamma$ 的定义，$|\mu|$ 恒为 16 的倍数 $\Rightarrow$ Poly1305 只遇满块。

输出密文 $C$ 与标签 $T$。解密对称：用同样的 $r\mathbin{\|}s$ 与 $\mu$ 重算 $T'$ 并（常数时间）比对，
通过后再 $P = \mathrm{ChaCha20\text{-}Dec}(K, 1, N, C)$。

---

## 参考

- **RFC 8439** — *ChaCha20 and Poly1305 for IETF Protocols*（§2.1-2.3 ChaCha20、§2.5 Poly1305、§2.8 AEAD）。
- D. J. Bernstein, *The Poly1305-AES message-authentication code*；及 poly1305-donna 的 radix-$2^{26}$ 实现。
